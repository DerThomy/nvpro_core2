/*
 * Copyright (c) 2023-2025, NVIDIA CORPORATION.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-FileCopyrightText: Copyright (c) 2023-2025, NVIDIA CORPORATION.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <array>

#include "gbuffers.hpp"
#include "debug_util.hpp"
#include "check_error.hpp"
#include "barriers.hpp"

nvvk::GBuffer::GBuffer(nvvk::GBuffer&& other) noexcept
{
  assert(m_info.allocator == nullptr && "Missing deinit()");
  std::swap(m_res, other.m_res);
  std::swap(m_size, other.m_size);
  std::swap(m_info, other.m_info);
  std::swap(m_descLayout, other.m_descLayout);
}

nvvk::GBuffer& nvvk::GBuffer::operator=(nvvk::GBuffer&& other) noexcept
{
  if(this != &other)
  {
    assert(m_info.allocator == nullptr && "Missing deinit()");
    std::swap(m_res, other.m_res);
    std::swap(m_size, other.m_size);
    std::swap(m_info, other.m_info);
    std::swap(m_descLayout, other.m_descLayout);
  }
  return *this;
}

nvvk::GBuffer::~GBuffer()
{
  assert(m_info.allocator == nullptr && "Missing deinit()");
}

void nvvk::GBuffer::init(const GBufferInitInfo& createInfo)
{
  assert(m_info.colorFormats.empty() && "Missing deinit()");  // The buffer must be cleared before creating a new one
  m_info = createInfo;                                        // Copy the creation info
}

void nvvk::GBuffer::deinit()
{
  deinitResources();
  m_res        = {};
  m_size       = {};
  m_descLayout = {};

  m_info = {};
}

VkResult nvvk::GBuffer::update(VkCommandBuffer cmd, VkExtent2D newSize, VkSampleCountFlagBits newSampleCount)
{
  if(newSize.width == m_size.width && newSize.height == m_size.height && newSampleCount == m_info.sampleCount)
  {
    return VK_SUCCESS;  // Nothing to do
  }

  vkDeviceWaitIdle(m_info.allocator->getDevice());
  deinitResources();
  m_size = newSize;
  m_info.sampleCount = newSampleCount;
  return initResources(cmd);
}

VkResult nvvk::GBuffer::update(VkCommandBuffer cmd, VkExtent2D newSize)
{
  return update(cmd, newSize, m_info.sampleCount);
}

VkDescriptorSet nvvk::GBuffer::getDescriptorSet(uint32_t i) const
{
  return m_res.uiDescriptorSets[i];
}

VkExtent2D nvvk::GBuffer::getSize() const
{
  return m_size;
}

VkImage nvvk::GBuffer::getColorImage(uint32_t i /*= 0*/) const
{
  return m_res.gBufferColor[i].image;
}

VkImage nvvk::GBuffer::getColorMSAAImage(uint32_t i) const
{
  return m_res.gBufferColorMSAA[i].image;
}

VkImage nvvk::GBuffer::getDepthImage() const
{
  return m_res.gBufferDepth.image;
}

VkImageView nvvk::GBuffer::getColorImageView(uint32_t i /*= 0*/) const
{
  return m_res.gBufferColor[i].descriptor.imageView;
}

VkImageView nvvk::GBuffer::getRenderImageView(uint32_t i) const
{
  if(m_info.sampleCount > VK_SAMPLE_COUNT_1_BIT && !m_res.gBufferColorMSAA.empty())
  {
    return m_res.gBufferColorMSAA[i].descriptor.imageView;
  }
  return m_res.gBufferColor[i].descriptor.imageView;
}

const VkDescriptorImageInfo& nvvk::GBuffer::getDescriptorImageInfo(uint32_t i /*= 0*/) const
{
  return m_res.gBufferColor[i].descriptor;
}

VkImageView nvvk::GBuffer::getDepthImageView() const
{
  return m_res.gBufferDepth.descriptor.imageView;
}

VkFormat nvvk::GBuffer::getColorFormat(uint32_t i /*= 0*/) const
{
  return m_info.colorFormats[i];
}

VkFormat nvvk::GBuffer::getDepthFormat() const
{
  return m_info.depthFormat;
}

VkSampleCountFlagBits nvvk::GBuffer::getSampleCount() const
{
  return m_info.sampleCount;
}

float nvvk::GBuffer::getAspectRatio() const
{
  if(m_size.height == 0)
    return 1.0f;
  return float(m_size.width) / float(m_size.height);
}

VkResult nvvk::GBuffer::initResources(VkCommandBuffer cmd)
{
  nvvk::DebugUtil&    dutil = nvvk::DebugUtil::getInstance();
  const VkImageLayout layout{VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkDevice            device = m_info.allocator->getDevice();
  bool                isMsaa = m_info.sampleCount > VK_SAMPLE_COUNT_1_BIT;

  const auto numColor = static_cast<uint32_t>(m_info.colorFormats.size());

  m_res.gBufferColor.resize(numColor);
  m_res.uiImageViews.resize(numColor);
  
  if(isMsaa)
    m_res.gBufferColorMSAA.resize(numColor);

  for(uint32_t c = 0; c < numColor; c++)
  {
    // Color image and view
    const VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
                                    | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    VkImageCreateInfo info = {
        .sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType   = VK_IMAGE_TYPE_2D,
        .format      = m_info.colorFormats[c],
        .extent      = {m_size.width, m_size.height, 1},
        .mipLevels   = 1,
        .arrayLayers = 1,
        .samples     = VK_SAMPLE_COUNT_1_BIT,
        .usage       = usage,
    };
    VkImageViewCreateInfo viewInfo = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D,
        .format           = m_info.colorFormats[c],
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1},
    };
    NVVK_FAIL_RETURN(m_info.allocator->createImage(m_res.gBufferColor[c], info, viewInfo));
    dutil.setObjectName(m_res.gBufferColor[c].image, "G-Color" + std::to_string(c));
    dutil.setObjectName(m_res.gBufferColor[c].descriptor.imageView, "G-Color" + std::to_string(c));

    // UI Image color view
    viewInfo.image        = m_res.gBufferColor[c].image;
    viewInfo.components.a = VK_COMPONENT_SWIZZLE_ONE;  // Forcing the VIEW to have a 1 in the alpha channel
    NVVK_FAIL_RETURN(vkCreateImageView(device, &viewInfo, nullptr, &m_res.uiImageViews[c]));
    dutil.setObjectName(m_res.uiImageViews[c], "UI G-Color" + std::to_string(c));

    // Set the sampler for the color attachment
    m_res.gBufferColor[c].descriptor.sampler = m_info.imageSampler;
    if(isMsaa)
    {
      // Transient usage allows tile-based GPUs to not allocate backing memory if possible, 
      // but we need standard TRANSFER/ATTACHMENT bits for clears and resolves.
      const VkImageUsageFlags msaaUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT; // DST for Clear
      
      info.samples = m_info.sampleCount;
      info.usage   = msaaUsage;
      
      NVVK_FAIL_RETURN(m_info.allocator->createImage(m_res.gBufferColorMSAA[c], info, viewInfo));
      dutil.setObjectName(m_res.gBufferColorMSAA[c].image, "G-Color-MSAA-" + std::to_string(c));
      dutil.setObjectName(m_res.gBufferColorMSAA[c].descriptor.imageView, "G-Color-MSAA-" + std::to_string(c));
    }
  }

  if(m_info.depthFormat != VK_FORMAT_UNDEFINED)
  {
    // Depth buffer
    const VkImageCreateInfo createInfo = {
        .sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType   = VK_IMAGE_TYPE_2D,
        .format      = m_info.depthFormat,
        .extent      = {m_size.width, m_size.height, 1},
        .mipLevels   = 1,
        .arrayLayers = 1,
        .samples     = m_info.sampleCount,
        .usage       = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
                 | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
    };
    const VkImageViewCreateInfo viewInfo = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D,
        .format           = m_info.depthFormat,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .levelCount = 1, .layerCount = 1},
    };
    NVVK_FAIL_RETURN(m_info.allocator->createImage(m_res.gBufferDepth, createInfo, viewInfo));
    dutil.setObjectName(m_res.gBufferDepth.image, "G-Depth");
    dutil.setObjectName(m_res.gBufferDepth.descriptor.imageView, "G-Depth");
  }

  {  // Clear all images and change layout
    std::vector<VkImageMemoryBarrier2> barriers;

    const VkClearColorValue                      clear_value = {{0.F, 0.F, 0.F, 0.F}};
    const std::array<VkImageSubresourceRange, 1> range       = {
          {{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1}}};
    const VkClearDepthStencilValue               clear_depth = {1.0f, 0};
    const VkImageSubresourceRange                range_depth = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};

    for(uint32_t c = 0; c < numColor; c++)
    {
      barriers.push_back(nvvk::makeImageMemoryBarrier({
          .image     = m_res.gBufferColor[c].image,
          .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
          .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
      }));

      // If MSAA exists, transition it too
      if(isMsaa)
      {
         barriers.push_back(nvvk::makeImageMemoryBarrier({
            .image     = m_res.gBufferColorMSAA[c].image,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
         }));
      }
    }

    if(m_res.gBufferDepth.image != VK_NULL_HANDLE)
    {
      barriers.push_back(nvvk::makeImageMemoryBarrier({
          .image            = m_res.gBufferDepth.image,
          .oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED,
          .newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          .subresourceRange = range_depth,
      }));
    }

    const VkDependencyInfo depInfoClear{.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                   .imageMemoryBarrierCount = (uint32_t)barriers.size(),
                                   .pImageMemoryBarriers    = barriers.data()};
    vkCmdPipelineBarrier2(cmd, &depInfoClear);
    barriers.clear();

    // Clear ALL images
    for(uint32_t c = 0; c < numColor; c++)
    {
      vkCmdClearColorImage(cmd, m_res.gBufferColor[c].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_value, uint32_t(range.size()), range.data());

      // 2. Clear MSAA Image (if exists)
      if(isMsaa)
      {
        vkCmdClearColorImage(cmd, m_res.gBufferColorMSAA[c].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_value, uint32_t(range.size()), range.data());
      }
    }

    if(m_res.gBufferDepth.image != VK_NULL_HANDLE)
    {
      vkCmdClearDepthStencilImage(cmd, m_res.gBufferDepth.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_depth, 1, &range_depth);
    }

    // Transition to Final Layout
    for(uint32_t c = 0; c < numColor; c++)
    {
      barriers.push_back(nvvk::makeImageMemoryBarrier({
          .image     = m_res.gBufferColor[c].image,
          .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          .newLayout = layout 
      }));
      m_res.gBufferColor[c].descriptor.imageLayout = layout;

      // 2. MSAA Image -> COLOR_ATTACHMENT_OPTIMAL (Ready for Rendering)
      if(isMsaa)
      {
         barriers.push_back(nvvk::makeImageMemoryBarrier({
            .image     = m_res.gBufferColorMSAA[c].image,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
         }));
         // MSAA descriptor layout is less important as we rarely sample it, but let's keep it consistent
         m_res.gBufferColorMSAA[c].descriptor.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; 
      }
    }

    if(m_res.gBufferDepth.image != VK_NULL_HANDLE)
    {
      barriers.push_back(nvvk::makeImageMemoryBarrier({
          .image            = m_res.gBufferDepth.image,
          .oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          .newLayout        = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
          .subresourceRange = range_depth,
      }));
      m_res.gBufferDepth.descriptor.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }

    const VkDependencyInfo depInfoLayout{.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                         .imageMemoryBarrierCount = (uint32_t)barriers.size(),
                                         .pImageMemoryBarriers    = barriers.data()};
    vkCmdPipelineBarrier2(cmd, &depInfoLayout);
  }

  // Descriptor Set for ImGUI
  if(m_info.descriptorPool)
  {
    m_res.uiDescriptorSets.resize(numColor);

    // Create descriptor set layout (used by ImGui)
    const VkDescriptorSetLayoutBinding binding = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT};
    const VkDescriptorSetLayoutCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 1, .pBindings = &binding};
    NVVK_FAIL_RETURN(vkCreateDescriptorSetLayout(device, &info, nullptr, &m_descLayout));

    // Same layout for all color attachments
    std::vector<VkDescriptorSetLayout> layouts(numColor, m_descLayout);

    // Allocate descriptor sets
    std::vector<VkDescriptorImageInfo> descImages(numColor);
    std::vector<VkWriteDescriptorSet>  writeDesc(numColor);
    const VkDescriptorSetAllocateInfo  allocInfos = {
         .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
         .descriptorPool     = m_info.descriptorPool,
         .descriptorSetCount = numColor,
         .pSetLayouts        = layouts.data(),
    };
    NVVK_FAIL_RETURN(vkAllocateDescriptorSets(device, &allocInfos, m_res.uiDescriptorSets.data()));

    // Update the descriptor sets
    for(uint32_t d = 0; d < numColor; ++d)
    {
      descImages[d] = {m_info.imageSampler, m_res.uiImageViews[d], layout};
      writeDesc[d]  = {
           .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
           .dstSet          = m_res.uiDescriptorSets[d],
           .descriptorCount = 1,
           .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
           .pImageInfo      = &descImages[d],
      };
    }
    vkUpdateDescriptorSets(device, uint32_t(m_res.uiDescriptorSets.size()), writeDesc.data(), 0, nullptr);
  }

  return VK_SUCCESS;
}

void nvvk::GBuffer::deinitResources()
{
  if(m_info.allocator == nullptr)
  {
    return;  // Nothing to do
  }

  VkDevice device = m_info.allocator->getDevice();
  if(m_info.descriptorPool && !m_res.uiDescriptorSets.empty())
  {
    vkFreeDescriptorSets(device, m_info.descriptorPool, uint32_t(m_res.uiDescriptorSets.size()), m_res.uiDescriptorSets.data());
    vkDestroyDescriptorSetLayout(device, m_descLayout, nullptr);
    m_res.uiDescriptorSets.clear();
    m_descLayout = VK_NULL_HANDLE;
  }

  for(nvvk::Image bc : m_res.gBufferColor)
  {
    m_info.allocator->destroyImage(bc);
  }
  m_res.gBufferColor.clear();

  for(nvvk::Image bc : m_res.gBufferColorMSAA)
  {
    m_info.allocator->destroyImage(bc);
  }
  m_res.gBufferColorMSAA.clear();

  if(m_res.gBufferDepth.image != VK_NULL_HANDLE)
  {
    m_info.allocator->destroyImage(m_res.gBufferDepth);
    m_res.gBufferDepth = {};
  }

  for(const VkImageView& view : m_res.uiImageViews)
  {
    vkDestroyImageView(device, view, nullptr);
  }
  m_res.uiImageViews.clear();
}


//--------------------------------------------------------------------------------------------------
// Usage example
//--------------------------------------------------------------------------------------------------
[[maybe_unused]] static void usage_GBuffer()
{
  nvvk::GBuffer gbuffer;

  nvvk::ResourceAllocator allocator;
  VkSampler               linearSampler = VK_NULL_HANDLE;  // EX: create a linear sampler
  VkDescriptorPool descriptorPool = VK_NULL_HANDLE;  // EX: create a descriptor pool or use the one from the app (m_app->getTextureDescriptorPool())

  // Create a G-buffer with two color images and one depth image.
  gbuffer.init({.allocator      = &allocator,
                .colorFormats   = {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R32G32B32A32_SFLOAT},
                .depthFormat    = VK_FORMAT_D32_SFLOAT,  // EX: use VK_FORMAT_UNDEFINED if no depth buffer is needed
                .imageSampler   = linearSampler,
                .descriptorPool = descriptorPool});

  // Setting the size
  VkCommandBuffer cmd = VK_NULL_HANDLE;  // EX: create a command buffer
  gbuffer.update(cmd, VkExtent2D{600, 480});

  // Get the image views
  VkImageView colorImageViewRgba8   = gbuffer.getColorImageView(0);
  VkImageView colorImageViewRgbaF32 = gbuffer.getColorImageView(1);
  VkImageView depthImageView        = gbuffer.getDepthImageView();

  // Display a G-Buffer using Dear ImGui like this (include <imgui.h>):
  // ImGui::Image((ImTextureID)gbuffer.getDescriptorSet(0), ImGui::GetContentRegionAvail());
}
