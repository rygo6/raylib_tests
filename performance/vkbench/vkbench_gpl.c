/*******************************************************************************************
*
*   vkbench_gpl - VK_EXT_graphics_pipeline_library fast-link draw benchmark (spike)
*
*   The GPL counterpart to vkbench.c. Instead of one monolithic VkPipeline, it pre-compiles the
*   four pipeline sub-libraries (vertex-input interface, pre-rasterization/VS, fragment/FS,
*   fragment-output interface) and FAST-LINKS them (no LINK_TIME_OPTIMIZATION) into an executable
*   pipeline - the model DXVK/vkd3d use to get static-pipeline draw performance while creating
*   novel state combinations cheaply at runtime.
*
*   It reports: (a) the fast-link time (should be tiny vs a monolithic compile), and (b) sustained
*   draw FPS for N cubes (should match the monolithic vkbench). This validates GPL as the rlvk
*   redesign target: static-pipeline per-draw cost + on-demand flexibility, no shader-object
*   command-record patching.
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
#define CHECK(x) do { VkResult _r=(x); if(_r!=VK_SUCCESS){ printf("VK error %d at %d\n",_r,__LINE__); exit(1);} } while(0)

typedef struct { float m[16]; } Mat4;
static Mat4 mul(Mat4 a,Mat4 b){ Mat4 r; for(int c=0;c<4;c++)for(int rw=0;rw<4;rw++){ float s=0; for(int k=0;k<4;k++) s+=a.m[k*4+rw]*b.m[c*4+k]; r.m[c*4+rw]=s;} return r; }
static Mat4 translate(float x,float y,float z){ Mat4 r={{1,0,0,0,0,1,0,0,0,0,1,0,x,y,z,1}}; return r; }
static Mat4 perspective(float fy,float a,float n,float f){ float t=1.0f/tanf(fy*0.5f); Mat4 r={{0}}; r.m[0]=t/a; r.m[5]=t; r.m[10]=(f+n)/(n-f); r.m[11]=-1; r.m[14]=(2*f*n)/(n-f); return r; }
static Mat4 lookAt(float ex,float ey,float ez,float cx,float cy,float cz){ float fx=cx-ex,fy=cy-ey,fz=cz-ez; float fl=sqrtf(fx*fx+fy*fy+fz*fz); fx/=fl;fy/=fl;fz/=fl; float sx=fy*0-fz*1,sy=fz*0-fx*0,sz=fx*1-fy*0; float sl=sqrtf(sx*sx+sy*sy+sz*sz); sx/=sl;sy/=sl;sz/=sl; float vx=sy*fz-sz*fy,vy=sz*fx-sx*fz,vz=sx*fy-sy*fx; Mat4 r={{sx,vx,-fx,0,sy,vy,-fy,0,sz,vz,-fz,0,-(sx*ex+sy*ey+sz*ez),-(vx*ex+vy*ey+vz*ez),(fx*ex+fy*ey+fz*ez),1}}; return r; }

typedef struct { float mvp[16]; float col[4]; } PC;
static uint32_t *readSpv(const char*p,size_t*s){ FILE*f=fopen(p,"rb"); if(!f){printf("open %s\n",p);exit(1);} fseek(f,0,SEEK_END); long z=ftell(f); fseek(f,0,SEEK_SET); uint32_t*b=malloc(z); fread(b,1,z,f); fclose(f); *s=z; return b; }
static uint32_t findMem(VkPhysicalDevice pd,uint32_t tb,VkMemoryPropertyFlags pr){ VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(pd,&mp); for(uint32_t i=0;i<mp.memoryTypeCount;i++) if((tb&(1<<i))&&(mp.memoryTypes[i].propertyFlags&pr)==pr) return i; exit(1); }
static double now_sec(void){ static LARGE_INTEGER fr={0}; if(!fr.QuadPart) QueryPerformanceFrequency(&fr); LARGE_INTEGER c; QueryPerformanceCounter(&c); return (double)c.QuadPart/fr.QuadPart; }

int main(int argc,char**argv){
    int N=4000; const char*e=getenv("RAYLIB_STRESS_LOAD"); if(e)N=atoi(e); if(argc>1)N=atoi(argv[1]); if(N<1)N=1;
    double runSec=3.0;
    // RLVK_LTO=1 -> link WITH VK_PIPELINE_CREATE_LINK_TIME_OPTIMIZATION_BIT_EXT (should match monolithic
    // GPU perf, at a much higher link cost). Default: fast-link (no optimization).
    int LTO = (getenv("RLVK_LTO") != NULL);
    VkPipelineCreateFlags subFlags = VK_PIPELINE_CREATE_LIBRARY_BIT_KHR | (LTO ? VK_PIPELINE_CREATE_RETAIN_LINK_TIME_OPTIMIZATION_INFO_BIT_EXT : 0);

    VkInstance inst; VkApplicationInfo app={VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.apiVersion=VK_API_VERSION_1_1;
    VkInstanceCreateInfo ici={VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ici.pApplicationInfo=&app; CHECK(vkCreateInstance(&ici,NULL,&inst));
    uint32_t nd=0; vkEnumeratePhysicalDevices(inst,&nd,NULL); VkPhysicalDevice*pds=malloc(nd*sizeof(*pds)); vkEnumeratePhysicalDevices(inst,&nd,pds);
    VkPhysicalDevice pd=pds[0]; for(uint32_t i=0;i<nd;i++){ VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(pds[i],&p); if(p.deviceType==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU){pd=pds[i];break;} }
    VkPhysicalDeviceProperties pdp; vkGetPhysicalDeviceProperties(pd,&pdp);
    uint32_t nq=0; vkGetPhysicalDeviceQueueFamilyProperties(pd,&nq,NULL); VkQueueFamilyProperties*qf=malloc(nq*sizeof(*qf)); vkGetPhysicalDeviceQueueFamilyProperties(pd,&nq,qf);
    uint32_t gq=0; for(uint32_t i=0;i<nq;i++) if(qf[i].queueFlags&VK_QUEUE_GRAPHICS_BIT){gq=i;break;}

    // Device with GPL enabled
    const char *devExt[]={ "VK_KHR_pipeline_library", "VK_EXT_graphics_pipeline_library" };
    VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT gplFeat={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_FEATURES_EXT}; gplFeat.graphicsPipelineLibrary=VK_TRUE;
    float prio=1; VkDeviceQueueCreateInfo qci={VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qci.queueFamilyIndex=gq; qci.queueCount=1; qci.pQueuePriorities=&prio;
    VkDeviceCreateInfo dci={VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; dci.pNext=&gplFeat; dci.queueCreateInfoCount=1; dci.pQueueCreateInfos=&qci; dci.enabledExtensionCount=2; dci.ppEnabledExtensionNames=devExt;
    VkDevice dev; CHECK(vkCreateDevice(pd,&dci,NULL,&dev)); VkQueue queue; vkGetDeviceQueue(dev,gq,0,&queue);

    // Offscreen color+depth + render pass + framebuffer (same as vkbench)
    VkImage cImg,dImg; VkDeviceMemory cMem,dMem; VkImageView cView,dView;
    {   VkImageCreateInfo ii={VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; ii.imageType=VK_IMAGE_TYPE_2D; ii.format=VK_FORMAT_R8G8B8A8_UNORM; ii.extent=(VkExtent3D){WIDTH,HEIGHT,1}; ii.mipLevels=1; ii.arrayLayers=1; ii.samples=1; ii.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        CHECK(vkCreateImage(dev,&ii,NULL,&cImg)); VkMemoryRequirements mr; vkGetImageMemoryRequirements(dev,cImg,&mr); VkMemoryAllocateInfo ai={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize=mr.size; ai.memoryTypeIndex=findMem(pd,mr.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); CHECK(vkAllocateMemory(dev,&ai,NULL,&cMem)); vkBindImageMemory(dev,cImg,cMem,0);
        VkImageViewCreateInfo vi={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vi.image=cImg; vi.viewType=VK_IMAGE_VIEW_TYPE_2D; vi.format=ii.format; vi.subresourceRange=(VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}; CHECK(vkCreateImageView(dev,&vi,NULL,&cView)); }
    {   VkImageCreateInfo ii={VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; ii.imageType=VK_IMAGE_TYPE_2D; ii.format=VK_FORMAT_D32_SFLOAT; ii.extent=(VkExtent3D){WIDTH,HEIGHT,1}; ii.mipLevels=1; ii.arrayLayers=1; ii.samples=1; ii.usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        CHECK(vkCreateImage(dev,&ii,NULL,&dImg)); VkMemoryRequirements mr; vkGetImageMemoryRequirements(dev,dImg,&mr); VkMemoryAllocateInfo ai={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize=mr.size; ai.memoryTypeIndex=findMem(pd,mr.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); CHECK(vkAllocateMemory(dev,&ai,NULL,&dMem)); vkBindImageMemory(dev,dImg,dMem,0);
        VkImageViewCreateInfo vi={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vi.image=dImg; vi.viewType=VK_IMAGE_VIEW_TYPE_2D; vi.format=ii.format; vi.subresourceRange=(VkImageSubresourceRange){VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1}; CHECK(vkCreateImageView(dev,&vi,NULL,&dView)); }
    VkRenderPass rp;
    {   VkAttachmentDescription att[2]={{0}}; att[0].format=VK_FORMAT_R8G8B8A8_UNORM; att[0].samples=1; att[0].loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR; att[0].storeOp=VK_ATTACHMENT_STORE_OP_STORE; att[0].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE; att[0].stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE; att[0].initialLayout=VK_IMAGE_LAYOUT_UNDEFINED; att[0].finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        att[1].format=VK_FORMAT_D32_SFLOAT; att[1].samples=1; att[1].loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR; att[1].storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE; att[1].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE; att[1].stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE; att[1].initialLayout=VK_IMAGE_LAYOUT_UNDEFINED; att[1].finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkAttachmentReference cr={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},dr={1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}; VkSubpassDescription sub={0}; sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS; sub.colorAttachmentCount=1; sub.pColorAttachments=&cr; sub.pDepthStencilAttachment=&dr;
        VkRenderPassCreateInfo ri={VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO}; ri.attachmentCount=2; ri.pAttachments=att; ri.subpassCount=1; ri.pSubpasses=&sub; CHECK(vkCreateRenderPass(dev,&ri,NULL,&rp)); }
    VkFramebuffer fb; { VkImageView v[2]={cView,dView}; VkFramebufferCreateInfo fi={VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO}; fi.renderPass=rp; fi.attachmentCount=2; fi.pAttachments=v; fi.width=WIDTH; fi.height=HEIGHT; fi.layers=1; CHECK(vkCreateFramebuffer(dev,&fi,NULL,&fb)); }

    static const float cube[]={ -0.5f,-0.5f,-0.5f,0.5f,-0.5f,-0.5f,0.5f,0.5f,-0.5f,0.5f,0.5f,-0.5f,-0.5f,0.5f,-0.5f,-0.5f,-0.5f,-0.5f, -0.5f,-0.5f,0.5f,0.5f,-0.5f,0.5f,0.5f,0.5f,0.5f,0.5f,0.5f,0.5f,-0.5f,0.5f,0.5f,-0.5f,-0.5f,0.5f, -0.5f,0.5f,0.5f,-0.5f,0.5f,-0.5f,-0.5f,-0.5f,-0.5f,-0.5f,-0.5f,-0.5f,-0.5f,-0.5f,0.5f,-0.5f,0.5f,0.5f, 0.5f,0.5f,0.5f,0.5f,0.5f,-0.5f,0.5f,-0.5f,-0.5f,0.5f,-0.5f,-0.5f,0.5f,-0.5f,0.5f,0.5f,0.5f,0.5f, -0.5f,-0.5f,-0.5f,0.5f,-0.5f,-0.5f,0.5f,-0.5f,0.5f,0.5f,-0.5f,0.5f,-0.5f,-0.5f,0.5f,-0.5f,-0.5f,-0.5f, -0.5f,0.5f,-0.5f,0.5f,0.5f,-0.5f,0.5f,0.5f,0.5f,0.5f,0.5f,0.5f,-0.5f,0.5f,0.5f,-0.5f,0.5f,-0.5f };
    VkBuffer vbo; VkDeviceMemory vboMem; { VkBufferCreateInfo bi={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bi.size=sizeof(cube); bi.usage=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT; CHECK(vkCreateBuffer(dev,&bi,NULL,&vbo)); VkMemoryRequirements mr; vkGetBufferMemoryRequirements(dev,vbo,&mr); VkMemoryAllocateInfo ai={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize=mr.size; ai.memoryTypeIndex=findMem(pd,mr.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT); CHECK(vkAllocateMemory(dev,&ai,NULL,&vboMem)); vkBindBufferMemory(dev,vbo,vboMem,0); void*m; vkMapMemory(dev,vboMem,0,sizeof(cube),0,&m); memcpy(m,cube,sizeof(cube)); vkUnmapMemory(dev,vboMem); }

    size_t vs,fs; uint32_t*vspv=readSpv("cube.vert.spv",&vs),*fspv=readSpv("cube.frag.spv",&fs);
    VkShaderModule vm,fm; VkShaderModuleCreateInfo smi={VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; smi.codeSize=vs; smi.pCode=vspv; CHECK(vkCreateShaderModule(dev,&smi,NULL,&vm)); smi.codeSize=fs; smi.pCode=fspv; CHECK(vkCreateShaderModule(dev,&smi,NULL,&fm));
    VkPushConstantRange pcr={VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(PC)}; VkPipelineLayoutCreateInfo pli={VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; pli.pushConstantRangeCount=1; pli.pPushConstantRanges=&pcr; VkPipelineLayout layout; CHECK(vkCreatePipelineLayout(dev,&pli,NULL,&layout));

    // ---- four GPL sub-libraries ----
    VkPipeline viLib,prLib,fsLib,foLib;
    // (1) vertex input interface
    {   VkVertexInputBindingDescription vb={0,3*sizeof(float),VK_VERTEX_INPUT_RATE_VERTEX}; VkVertexInputAttributeDescription va={0,0,VK_FORMAT_R32G32B32_SFLOAT,0};
        VkPipelineVertexInputStateCreateInfo vi={VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO}; vi.vertexBindingDescriptionCount=1; vi.pVertexBindingDescriptions=&vb; vi.vertexAttributeDescriptionCount=1; vi.pVertexAttributeDescriptions=&va;
        VkPipelineInputAssemblyStateCreateInfo ia={VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkGraphicsPipelineLibraryCreateInfoEXT g={VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT}; g.flags=VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT;
        VkGraphicsPipelineCreateInfo p={VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO}; p.pNext=&g; p.flags=subFlags; p.pVertexInputState=&vi; p.pInputAssemblyState=&ia;
        CHECK(vkCreateGraphicsPipelines(dev,VK_NULL_HANDLE,1,&p,NULL,&viLib)); }
    // (2) pre-rasterization shaders (VS + viewport + raster)
    {   VkPipelineShaderStageCreateInfo st={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}; st.stage=VK_SHADER_STAGE_VERTEX_BIT; st.module=vm; st.pName="main";
        VkViewport vp={0,0,WIDTH,HEIGHT,0,1}; VkRect2D sc={{0,0},{WIDTH,HEIGHT}}; VkPipelineViewportStateCreateInfo vps={VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; vps.viewportCount=1; vps.pViewports=&vp; vps.scissorCount=1; vps.pScissors=&sc;
        VkPipelineRasterizationStateCreateInfo rs={VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO}; rs.polygonMode=VK_POLYGON_MODE_FILL; rs.cullMode=VK_CULL_MODE_BACK_BIT; rs.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth=1;
        VkGraphicsPipelineLibraryCreateInfoEXT g={VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT}; g.flags=VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT;
        VkGraphicsPipelineCreateInfo p={VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO}; p.pNext=&g; p.flags=subFlags; p.stageCount=1; p.pStages=&st; p.pViewportState=&vps; p.pRasterizationState=&rs; p.layout=layout; p.renderPass=rp; p.subpass=0;
        CHECK(vkCreateGraphicsPipelines(dev,VK_NULL_HANDLE,1,&p,NULL,&prLib)); }
    // (3) fragment shader (FS + depth)
    {   VkPipelineShaderStageCreateInfo st={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}; st.stage=VK_SHADER_STAGE_FRAGMENT_BIT; st.module=fm; st.pName="main";
        VkPipelineDepthStencilStateCreateInfo ds={VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO}; ds.depthTestEnable=VK_TRUE; ds.depthWriteEnable=VK_TRUE; ds.depthCompareOp=VK_COMPARE_OP_LESS;
        VkGraphicsPipelineLibraryCreateInfoEXT g={VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT}; g.flags=VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT;
        VkGraphicsPipelineCreateInfo p={VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO}; p.pNext=&g; p.flags=subFlags; p.stageCount=1; p.pStages=&st; p.pDepthStencilState=&ds; p.layout=layout; p.renderPass=rp; p.subpass=0;
        CHECK(vkCreateGraphicsPipelines(dev,VK_NULL_HANDLE,1,&p,NULL,&fsLib)); }
    // (4) fragment output interface (blend + multisample)
    {   VkPipelineColorBlendAttachmentState cba={0}; cba.colorWriteMask=0xF; VkPipelineColorBlendStateCreateInfo cb={VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; cb.attachmentCount=1; cb.pAttachments=&cba;
        VkPipelineMultisampleStateCreateInfo ms={VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
        VkGraphicsPipelineLibraryCreateInfoEXT g={VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT}; g.flags=VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT;
        VkGraphicsPipelineCreateInfo p={VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO}; p.pNext=&g; p.flags=subFlags; p.pColorBlendState=&cb; p.pMultisampleState=&ms; p.layout=layout; p.renderPass=rp; p.subpass=0;
        CHECK(vkCreateGraphicsPipelines(dev,VK_NULL_HANDLE,1,&p,NULL,&foLib)); }

    // ---- FAST-LINK (no LINK_TIME_OPTIMIZATION) ----
    VkPipeline pipe; double lt0=now_sec();
    {   VkPipeline libs[4]={viLib,prLib,fsLib,foLib}; VkPipelineLibraryCreateInfoKHR li={VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR}; li.libraryCount=4; li.pLibraries=libs;
        VkGraphicsPipelineCreateInfo p={VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO}; p.pNext=&li; p.layout=layout;
        p.flags = LTO ? VK_PIPELINE_CREATE_LINK_TIME_OPTIMIZATION_BIT_EXT : 0;
        CHECK(vkCreateGraphicsPipelines(dev,VK_NULL_HANDLE,1,&p,NULL,&pipe)); }
    double linkMs=(now_sec()-lt0)*1000.0;

    VkCommandPool pool; VkCommandPoolCreateInfo cpi={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; cpi.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; cpi.queueFamilyIndex=gq; CHECK(vkCreateCommandPool(dev,&cpi,NULL,&pool));
    VkCommandBuffer cmd; VkCommandBufferAllocateInfo cai={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; cai.commandPool=pool; cai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount=1; CHECK(vkAllocateCommandBuffers(dev,&cai,&cmd));
    VkFence fence; VkFenceCreateInfo fci={VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; CHECK(vkCreateFence(dev,&fci,NULL,&fence));

    int side=(int)ceilf(cbrtf((float)N)); Mat4 proj=perspective(60.0f*3.14159f/180.0f,(float)WIDTH/HEIGHT,0.1f,1000.0f); Mat4*models=malloc(N*sizeof(Mat4));
    for(int i=0;i<N;i++){int x=i%side,y=(i/side)%side,z=i/(side*side); models[i]=translate((x-side*0.5f)*1.6f,(y-side*0.5f)*1.6f,(z-side*0.5f)*1.6f);}

    printf("vkbench_gpl (GPL %s, headless) : device=%s  N=%d cubes\n", LTO ? "LINK-TIME-OPTIMIZED" : "fast-link", pdp.deviceName, N);
    printf("  link time: %.3f ms  (%s)\n", linkMs, LTO ? "with optimization pass" : "fast-link, no optimization");

    VkClearValue cl[2]; cl[0].color=(VkClearColorValue){{0.05f,0.06f,0.08f,1}}; cl[1].depthStencil=(VkClearDepthStencilValue){1,0};
    long frames=0; double t0=now_sec(),t=t0,warm=t0+0.5,mStart=0;
    while((t=now_sec())-t0<runSec+0.5){
        float ct=(float)(t-t0)*0.2f; Mat4 vp=mul(proj,lookAt(cosf(ct)*55.0f,20.0f,sinf(ct)*55.0f,0,0,0));
        vkResetCommandBuffer(cmd,0); VkCommandBufferBeginInfo bi={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; vkBeginCommandBuffer(cmd,&bi);
        VkRenderPassBeginInfo rb={VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO}; rb.renderPass=rp; rb.framebuffer=fb; rb.renderArea=(VkRect2D){{0,0},{WIDTH,HEIGHT}}; rb.clearValueCount=2; rb.pClearValues=cl; vkCmdBeginRenderPass(cmd,&rb,VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipe); VkDeviceSize off=0; vkCmdBindVertexBuffers(cmd,0,1,&vbo,&off);
        for(int i=0;i<N;i++){ Mat4 mvp=mul(vp,models[i]); PC pc; memcpy(pc.mvp,mvp.m,64); pc.col[0]=(float)((i*37)%256)/255.0f; pc.col[1]=(float)((i*61)%256)/255.0f; pc.col[2]=(float)((i*97)%256)/255.0f; pc.col[3]=1; vkCmdPushConstants(cmd,layout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(PC),&pc); vkCmdDraw(cmd,36,1,0,0); }
        vkCmdEndRenderPass(cmd); vkEndCommandBuffer(cmd);
        VkSubmitInfo si={VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.commandBufferCount=1; si.pCommandBuffers=&cmd; vkQueueSubmit(queue,1,&si,fence); vkWaitForFences(dev,1,&fence,VK_TRUE,UINT64_MAX); vkResetFences(dev,1,&fence);
        if(t<warm) continue; if(mStart==0)mStart=now_sec(); frames++;
    }
    double meas=now_sec()-mStart;
    printf("RESULT gpl-fastlink  N=%d : %.1f fps  (%.4f us/draw)\n", N, frames/meas, (meas/frames)*1e6/N);
    vkDeviceWaitIdle(dev); return 0;
}
