#include "vkcommon.h"
int main(int argc,char**argv) {
 if(argc!=2)return 2;
 struct vkctx c;
 VkPhysicalDeviceRobustness2FeaturesEXT r={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT,NULL,VK_TRUE,VK_FALSE,VK_FALSE};
 VkPhysicalDeviceFeatures2 f={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,&r}; f.features.robustBufferAccess=VK_TRUE;
 const char* ex[]={VK_EXT_ROBUSTNESS_2_EXTENSION_NAME,"VK_KHR_portability_subset"}; vkctx_init(&c,&f,ex,2);
 VkDescriptorSetLayoutBinding b[3]={{0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL},{1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL},{2,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL}};
 VkDescriptorSetLayoutCreateInfo li={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,NULL,0,3,b};VkDescriptorSetLayout l;CHECK(vkCreateDescriptorSetLayout(c.device,&li,NULL,&l));
 VkPipelineLayoutCreateInfo pi={VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,NULL,0,1,&l};VkPipelineLayout pl;CHECK(vkCreatePipelineLayout(c.device,&pi,NULL,&pl));
 VkShaderModule sm=vkctx_shader(&c,argv[1]);
 VkComputePipelineCreateInfo ci={VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};ci.stage=(VkPipelineShaderStageCreateInfo){VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,NULL,0,VK_SHADER_STAGE_COMPUTE_BIT,sm,"main",NULL};ci.layout=pl;VkPipeline p;CHECK(vkCreateComputePipelines(c.device,0,1,&ci,NULL,&p));
 struct vkbuf src=vkctx_buffer(&c,256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),out=vkctx_buffer(&c,256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),cb=vkctx_buffer(&c,256,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
 for(unsigned i=0;i<64;i++){((float*)src.map)[i]=100+i;((float*)cb.map)[i]=200+i;((float*)out.map)[i]=-999;}
 VkDescriptorPoolSize sz[2]={{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,2},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1}};VkDescriptorPoolCreateInfo di={VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,NULL,0,1,2,sz};VkDescriptorPool dp;CHECK(vkCreateDescriptorPool(c.device,&di,NULL,&dp));
 VkDescriptorSetAllocateInfo ai={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,NULL,dp,1,&l};VkDescriptorSet ds;CHECK(vkAllocateDescriptorSets(c.device,&ai,&ds));
 VkDescriptorBufferInfo bi[3]={{src.buffer,0,64},{out.buffer,0,256},{cb.buffer,0,64}};VkWriteDescriptorSet w[3]={0};for(unsigned i=0;i<3;i++){w[i]=(VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,NULL,ds,i,0,1,b[i].descriptorType,NULL,&bi[i],NULL};}vkUpdateDescriptorSets(c.device,3,w,0,NULL);
 VkCommandBuffer cmd=vkctx_begin(&c);vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,p);vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pl,0,1,&ds,0,NULL);vkCmdDispatch(cmd,1,1,1);vkctx_submit(&c,cmd);
 for(unsigned i=0;i<64;i++){float want=(i%32<16)?((i<32?100:200)+i%32):0;float got=((float*)out.map)[i];if(got!=want){fprintf(stderr,"FAIL word%u got%g want%g\n",i,got,want);failures++;}}
 printf("%s: 64 float readbacks; device and constant vec4 loads, valid and OOB\n",failures?"FAIL":"PASS");
 vkDeviceWaitIdle(c.device);vkDestroyDevice(c.device,NULL);vkDestroyInstance(c.instance,NULL);return failures?1:0;
}
