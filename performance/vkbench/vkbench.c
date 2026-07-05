/*******************************************************************************************
*
*   vkbench - isolated Vulkan-1.0-style static-pipeline draw-call benchmark
*
*   Tests the hypothesis: is a classic Vulkan 1.0 renderer (a single monolithic VkPipeline with
*   EVERYTHING baked in - render pass, fixed viewport/scissor, depth/blend/raster state, shaders
*   as VkShaderModules - bound once, no dynamic state, no VK_EXT_shader_object) faster per draw
*   call than rlvk's shader-object + fully-dynamic-state model, and faster than rlgl?
*
*   It draws N cubes (RAYLIB_STRESS_LOAD, default 4000), one vkCmdDraw per cube with a per-cube
*   push-constant MVP, re-recording the command buffer each frame (like a real immediate renderer),
*   HEADLESS (offscreen color+depth, no swapchain/present) so it measures purely the draw path.
*   Reports sustained "FPS" (record+submit+wait iterations/sec) to compare against the rlgl/rlvk
*   bench_drawcalls numbers at the same N.
*
********************************************************************************************/

#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <windows.h>

#define WIDTH  1280
#define HEIGHT 720
#define CHECK(x) do { VkResult _r = (x); if (_r != VK_SUCCESS) { printf("VK error %d at %s:%d\n", _r, __FILE__, __LINE__); exit(1); } } while (0)

//------------------------------------------------------------- minimal mat4 (column-major float[16])
typedef struct { float m[16]; } Mat4;
static Mat4 mul(Mat4 a, Mat4 b){ Mat4 r; for(int c=0;c<4;c++)for(int rw=0;rw<4;rw++){ float s=0; for(int k=0;k<4;k++) s+=a.m[k*4+rw]*b.m[c*4+k]; r.m[c*4+rw]=s; } return r; }
static Mat4 translate(float x,float y,float z){ Mat4 r={{1,0,0,0, 0,1,0,0, 0,0,1,0, x,y,z,1}}; return r; }
static Mat4 perspective(float fovy,float asp,float n,float f){ float t=1.0f/tanf(fovy*0.5f); Mat4 r={{0}}; r.m[0]=t/asp; r.m[5]=t; r.m[10]=(f+n)/(n-f); r.m[11]=-1; r.m[14]=(2*f*n)/(n-f); return r; }
static Mat4 lookAt(float ex,float ey,float ez,float cx,float cy,float cz){
    float fx=cx-ex,fy=cy-ey,fz=cz-ez; float fl=sqrtf(fx*fx+fy*fy+fz*fz); fx/=fl;fy/=fl;fz/=fl;
    float ux=0,uy=1,uz=0; float sx=fy*uz-fz*uy, sy=fz*ux-fx*uz, sz=fx*uy-fy*ux; float sl=sqrtf(sx*sx+sy*sy+sz*sz); sx/=sl;sy/=sl;sz/=sl;
    float vx=sy*fz-sz*fy, vy=sz*fx-sx*fz, vz=sx*fy-sy*fx;
    Mat4 r={{ sx,vx,-fx,0, sy,vy,-fy,0, sz,vz,-fz,0, -(sx*ex+sy*ey+sz*ez),-(vx*ex+vy*ey+vz*ez),(fx*ex+fy*ey+fz*ez),1 }}; return r; }

//------------------------------------------------------------- push constant block (matches shader)
typedef struct { float mvp[16]; float col[4]; } PC;

static uint32_t *readSpv(const char *path, size_t *sizeOut){
    FILE *f=fopen(path,"rb"); if(!f){ printf("cannot open %s\n",path); exit(1);} fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    uint32_t *buf=(uint32_t*)malloc(sz); fread(buf,1,sz,f); fclose(f); *sizeOut=sz; return buf; }

static uint32_t findMem(VkPhysicalDevice pd, uint32_t typeBits, VkMemoryPropertyFlags props){
    VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(pd,&mp);
    for(uint32_t i=0;i<mp.memoryTypeCount;i++) if((typeBits&(1<<i)) && (mp.memoryTypes[i].propertyFlags&props)==props) return i;
    printf("no mem type\n"); exit(1); }

static double now_sec(void){ static LARGE_INTEGER fr={0}; if(!fr.QuadPart) QueryPerformanceFrequency(&fr); LARGE_INTEGER c; QueryPerformanceCounter(&c); return (double)c.QuadPart/(double)fr.QuadPart; }

int main(int argc, char **argv){
    int N = 4000;
    const char *e = getenv("RAYLIB_STRESS_LOAD"); if(e) N=atoi(e); if(argc>1) N=atoi(argv[1]); if(N<1) N=1;
    double runSec = 3.0;

    // ---- instance ----
    VkInstance inst;
    VkApplicationInfo app={VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.apiVersion=VK_API_VERSION_1_0;
    VkInstanceCreateInfo ici={VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ici.pApplicationInfo=&app;
    CHECK(vkCreateInstance(&ici,NULL,&inst));

    // ---- physical device: prefer discrete ----
    uint32_t nd=0; vkEnumeratePhysicalDevices(inst,&nd,NULL);
    VkPhysicalDevice *pds=malloc(nd*sizeof(*pds)); vkEnumeratePhysicalDevices(inst,&nd,pds);
    VkPhysicalDevice pd=pds[0];
    for(uint32_t i=0;i<nd;i++){ VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(pds[i],&p); if(p.deviceType==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU){ pd=pds[i]; break; } }
    VkPhysicalDeviceProperties pdp; vkGetPhysicalDeviceProperties(pd,&pdp);

    // ---- queue family ----
    uint32_t nq=0; vkGetPhysicalDeviceQueueFamilyProperties(pd,&nq,NULL);
    VkQueueFamilyProperties *qf=malloc(nq*sizeof(*qf)); vkGetPhysicalDeviceQueueFamilyProperties(pd,&nq,qf);
    uint32_t gq=0; for(uint32_t i=0;i<nq;i++) if(qf[i].queueFlags&VK_QUEUE_GRAPHICS_BIT){ gq=i; break; }

    // ---- device ----
    float prio=1.0f; VkDeviceQueueCreateInfo qci={VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qci.queueFamilyIndex=gq; qci.queueCount=1; qci.pQueuePriorities=&prio;
    VkDeviceCreateInfo dci={VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; dci.queueCreateInfoCount=1; dci.pQueueCreateInfos=&qci;
    VkDevice dev; CHECK(vkCreateDevice(pd,&dci,NULL,&dev));
    VkQueue queue; vkGetDeviceQueue(dev,gq,0,&queue);

    // ---- offscreen color + depth ----
    VkImage colorImg, depthImg; VkDeviceMemory colorMem, depthMem; VkImageView colorView, depthView;
    {   VkImageCreateInfo ii={VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; ii.imageType=VK_IMAGE_TYPE_2D; ii.format=VK_FORMAT_R8G8B8A8_UNORM;
        ii.extent=(VkExtent3D){WIDTH,HEIGHT,1}; ii.mipLevels=1; ii.arrayLayers=1; ii.samples=VK_SAMPLE_COUNT_1_BIT; ii.tiling=VK_IMAGE_TILING_OPTIMAL;
        ii.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; ii.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
        CHECK(vkCreateImage(dev,&ii,NULL,&colorImg));
        VkMemoryRequirements mr; vkGetImageMemoryRequirements(dev,colorImg,&mr);
        VkMemoryAllocateInfo ai={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize=mr.size; ai.memoryTypeIndex=findMem(pd,mr.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        CHECK(vkAllocateMemory(dev,&ai,NULL,&colorMem)); vkBindImageMemory(dev,colorImg,colorMem,0);
        VkImageViewCreateInfo vi={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vi.image=colorImg; vi.viewType=VK_IMAGE_VIEW_TYPE_2D; vi.format=ii.format;
        vi.subresourceRange=(VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}; CHECK(vkCreateImageView(dev,&vi,NULL,&colorView));
    }
    {   VkImageCreateInfo ii={VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; ii.imageType=VK_IMAGE_TYPE_2D; ii.format=VK_FORMAT_D32_SFLOAT;
        ii.extent=(VkExtent3D){WIDTH,HEIGHT,1}; ii.mipLevels=1; ii.arrayLayers=1; ii.samples=VK_SAMPLE_COUNT_1_BIT; ii.tiling=VK_IMAGE_TILING_OPTIMAL;
        ii.usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT; ii.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
        CHECK(vkCreateImage(dev,&ii,NULL,&depthImg));
        VkMemoryRequirements mr; vkGetImageMemoryRequirements(dev,depthImg,&mr);
        VkMemoryAllocateInfo ai={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize=mr.size; ai.memoryTypeIndex=findMem(pd,mr.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        CHECK(vkAllocateMemory(dev,&ai,NULL,&depthMem)); vkBindImageMemory(dev,depthImg,depthMem,0);
        VkImageViewCreateInfo vi={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vi.image=depthImg; vi.viewType=VK_IMAGE_VIEW_TYPE_2D; vi.format=ii.format;
        vi.subresourceRange=(VkImageSubresourceRange){VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1}; CHECK(vkCreateImageView(dev,&vi,NULL,&depthView));
    }

    // ---- render pass (classic 1.0) ----
    VkRenderPass rp;
    {   VkAttachmentDescription att[2]={{0}};
        att[0].format=VK_FORMAT_R8G8B8A8_UNORM; att[0].samples=VK_SAMPLE_COUNT_1_BIT; att[0].loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR; att[0].storeOp=VK_ATTACHMENT_STORE_OP_STORE;
        att[0].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE; att[0].stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE; att[0].initialLayout=VK_IMAGE_LAYOUT_UNDEFINED; att[0].finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        att[1].format=VK_FORMAT_D32_SFLOAT; att[1].samples=VK_SAMPLE_COUNT_1_BIT; att[1].loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR; att[1].storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att[1].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE; att[1].stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE; att[1].initialLayout=VK_IMAGE_LAYOUT_UNDEFINED; att[1].finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkAttachmentReference cref={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}, dref={1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub={0}; sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS; sub.colorAttachmentCount=1; sub.pColorAttachments=&cref; sub.pDepthStencilAttachment=&dref;
        VkRenderPassCreateInfo rpi={VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO}; rpi.attachmentCount=2; rpi.pAttachments=att; rpi.subpassCount=1; rpi.pSubpasses=&sub;
        CHECK(vkCreateRenderPass(dev,&rpi,NULL,&rp));
    }
    VkFramebuffer fb;
    {   VkImageView views[2]={colorView,depthView}; VkFramebufferCreateInfo fi={VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO}; fi.renderPass=rp; fi.attachmentCount=2; fi.pAttachments=views; fi.width=WIDTH; fi.height=HEIGHT; fi.layers=1;
        CHECK(vkCreateFramebuffer(dev,&fi,NULL,&fb)); }

    // ---- cube vertex buffer (36 verts, pos only) ----
    static const float cube[]={ -0.5f,-0.5f,-0.5f, 0.5f,-0.5f,-0.5f, 0.5f,0.5f,-0.5f, 0.5f,0.5f,-0.5f, -0.5f,0.5f,-0.5f, -0.5f,-0.5f,-0.5f,
        -0.5f,-0.5f,0.5f, 0.5f,-0.5f,0.5f, 0.5f,0.5f,0.5f, 0.5f,0.5f,0.5f, -0.5f,0.5f,0.5f, -0.5f,-0.5f,0.5f,
        -0.5f,0.5f,0.5f, -0.5f,0.5f,-0.5f, -0.5f,-0.5f,-0.5f, -0.5f,-0.5f,-0.5f, -0.5f,-0.5f,0.5f, -0.5f,0.5f,0.5f,
        0.5f,0.5f,0.5f, 0.5f,0.5f,-0.5f, 0.5f,-0.5f,-0.5f, 0.5f,-0.5f,-0.5f, 0.5f,-0.5f,0.5f, 0.5f,0.5f,0.5f,
        -0.5f,-0.5f,-0.5f, 0.5f,-0.5f,-0.5f, 0.5f,-0.5f,0.5f, 0.5f,-0.5f,0.5f, -0.5f,-0.5f,0.5f, -0.5f,-0.5f,-0.5f,
        -0.5f,0.5f,-0.5f, 0.5f,0.5f,-0.5f, 0.5f,0.5f,0.5f, 0.5f,0.5f,0.5f, -0.5f,0.5f,0.5f, -0.5f,0.5f,-0.5f };
    VkBuffer vbo; VkDeviceMemory vboMem;
    {   VkBufferCreateInfo bi={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bi.size=sizeof(cube); bi.usage=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT; bi.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        CHECK(vkCreateBuffer(dev,&bi,NULL,&vbo)); VkMemoryRequirements mr; vkGetBufferMemoryRequirements(dev,vbo,&mr);
        VkMemoryAllocateInfo ai={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize=mr.size; ai.memoryTypeIndex=findMem(pd,mr.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        CHECK(vkAllocateMemory(dev,&ai,NULL,&vboMem)); vkBindBufferMemory(dev,vbo,vboMem,0);
        void *map; vkMapMemory(dev,vboMem,0,sizeof(cube),0,&map); memcpy(map,cube,sizeof(cube)); vkUnmapMemory(dev,vboMem); }

    // ---- shader modules ----
    size_t vsz,fsz; uint32_t *vspv=readSpv("cube.vert.spv",&vsz), *fspv=readSpv("cube.frag.spv",&fsz);
    VkShaderModule vs,fs; VkShaderModuleCreateInfo smi={VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smi.codeSize=vsz; smi.pCode=vspv; CHECK(vkCreateShaderModule(dev,&smi,NULL,&vs));
    smi.codeSize=fsz; smi.pCode=fspv; CHECK(vkCreateShaderModule(dev,&smi,NULL,&fs));

    // ---- pipeline layout (push constants) ----
    VkPushConstantRange pcr={VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(PC)};
    VkPipelineLayoutCreateInfo pli={VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; pli.pushConstantRangeCount=1; pli.pPushConstantRanges=&pcr;
    VkPipelineLayout layout; CHECK(vkCreatePipelineLayout(dev,&pli,NULL,&layout));

    // ---- static graphics pipeline: EVERYTHING baked, no dynamic state ----
    VkPipeline pipe;
    {   VkPipelineShaderStageCreateInfo stages[2]={{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
        stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT; stages[0].module=vs; stages[0].pName="main";
        stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module=fs; stages[1].pName="main";
        VkVertexInputBindingDescription vb={0,3*sizeof(float),VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription va={0,0,VK_FORMAT_R32G32B32_SFLOAT,0};
        VkPipelineVertexInputStateCreateInfo vi={VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO}; vi.vertexBindingDescriptionCount=1; vi.pVertexBindingDescriptions=&vb; vi.vertexAttributeDescriptionCount=1; vi.pVertexAttributeDescriptions=&va;
        VkPipelineInputAssemblyStateCreateInfo ia={VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport vp={0,0,WIDTH,HEIGHT,0,1}; VkRect2D sc={{0,0},{WIDTH,HEIGHT}};
        VkPipelineViewportStateCreateInfo vps={VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; vps.viewportCount=1; vps.pViewports=&vp; vps.scissorCount=1; vps.pScissors=&sc;
        VkPipelineRasterizationStateCreateInfo rs={VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO}; rs.polygonMode=VK_POLYGON_MODE_FILL; rs.cullMode=VK_CULL_MODE_BACK_BIT; rs.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth=1.0f;
        VkPipelineMultisampleStateCreateInfo ms={VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo ds={VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO}; ds.depthTestEnable=VK_TRUE; ds.depthWriteEnable=VK_TRUE; ds.depthCompareOp=VK_COMPARE_OP_LESS;
        VkPipelineColorBlendAttachmentState cba={0}; cba.colorWriteMask=0xF;
        VkPipelineColorBlendStateCreateInfo cb={VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; cb.attachmentCount=1; cb.pAttachments=&cba;
        VkGraphicsPipelineCreateInfo gp={VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO}; gp.stageCount=2; gp.pStages=stages; gp.pVertexInputState=&vi; gp.pInputAssemblyState=&ia;
        gp.pViewportState=&vps; gp.pRasterizationState=&rs; gp.pMultisampleState=&ms; gp.pDepthStencilState=&ds; gp.pColorBlendState=&cb; gp.layout=layout; gp.renderPass=rp; gp.subpass=0;
        CHECK(vkCreateGraphicsPipelines(dev,VK_NULL_HANDLE,1,&gp,NULL,&pipe));
    }

    // ---- command pool/buffer + fence ----
    VkCommandPool pool; VkCommandPoolCreateInfo cpi={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; cpi.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; cpi.queueFamilyIndex=gq;
    CHECK(vkCreateCommandPool(dev,&cpi,NULL,&pool));
    VkCommandBuffer cmd; VkCommandBufferAllocateInfo cai={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; cai.commandPool=pool; cai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount=1;
    CHECK(vkAllocateCommandBuffers(dev,&cai,&cmd));
    VkFence fence; VkFenceCreateInfo fci={VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; CHECK(vkCreateFence(dev,&fci,NULL,&fence));

    // ---- static per-cube model matrices (grid) ----
    int side=(int)ceilf(cbrtf((float)N));
    Mat4 proj=perspective(60.0f*3.14159f/180.0f,(float)WIDTH/HEIGHT,0.1f,1000.0f);
    Mat4 *models=malloc(N*sizeof(Mat4));
    for(int i=0;i<N;i++){ int x=i%side,y=(i/side)%side,z=i/(side*side); models[i]=translate((x-side*0.5f)*1.6f,(y-side*0.5f)*1.6f,(z-side*0.5f)*1.6f); }

    printf("vkbench (static VkPipeline, headless) : device=%s  N=%d cubes  %.0fs\n", pdp.deviceName, N, runSec);

    // ---- render loop: re-record each frame (per-cube push-constant mvp), submit, wait ----
    VkClearValue clears[2]; clears[0].color=(VkClearColorValue){{0.05f,0.06f,0.08f,1}}; clears[1].depthStencil=(VkClearDepthStencilValue){1.0f,0};
    long frames=0; double t0=now_sec(), t=t0, warmEnd=t0+0.5, mStart=0;
    while ((t=now_sec()) - t0 < runSec + 0.5){
        float ct=(float)(t-t0)*0.2f;
        Mat4 view=lookAt(cosf(ct)*55.0f,20.0f,sinf(ct)*55.0f, 0,0,0);
        Mat4 vp=mul(proj,view);

        vkResetCommandBuffer(cmd,0);
        VkCommandBufferBeginInfo bi={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; vkBeginCommandBuffer(cmd,&bi);
        VkRenderPassBeginInfo rpb={VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO}; rpb.renderPass=rp; rpb.framebuffer=fb; rpb.renderArea=(VkRect2D){{0,0},{WIDTH,HEIGHT}}; rpb.clearValueCount=2; rpb.pClearValues=clears;
        vkCmdBeginRenderPass(cmd,&rpb,VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipe);          // ONCE
        VkDeviceSize off=0; vkCmdBindVertexBuffers(cmd,0,1,&vbo,&off);        // ONCE
        for(int i=0;i<N;i++){
            Mat4 mvp=mul(vp,models[i]); PC pc; memcpy(pc.mvp,mvp.m,64);
            pc.col[0]=(float)((i*37)%256)/255.0f; pc.col[1]=(float)((i*61)%256)/255.0f; pc.col[2]=(float)((i*97)%256)/255.0f; pc.col[3]=1.0f;
            vkCmdPushConstants(cmd,layout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(PC),&pc);
            vkCmdDraw(cmd,36,1,0,0);
        }
        vkCmdEndRenderPass(cmd); vkEndCommandBuffer(cmd);

        VkSubmitInfo si={VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.commandBufferCount=1; si.pCommandBuffers=&cmd;
        vkQueueSubmit(queue,1,&si,fence); vkWaitForFences(dev,1,&fence,VK_TRUE,UINT64_MAX); vkResetFences(dev,1,&fence);

        if (t < warmEnd) continue;              // warm-up
        if (mStart==0) mStart=now_sec();
        frames++;
    }
    double measured=now_sec()-mStart;
    printf("RESULT static-pipeline  N=%d : %.1f fps  (%.4f ms/frame,  %.4f us/draw)\n",
        N, frames/measured, measured/frames*1000.0, (measured/frames)*1e6/N);

    vkDeviceWaitIdle(dev);
    return 0;
}
