/*
* Copyright (c) 2025, NVIDIA CORPORATION.  All rights reserved.
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
* SPDX-FileCopyrightText: Copyright (c) 2024, NVIDIA CORPORATION.
* SPDX-License-Identifier: Apache-2.0
*/

#include <vulkan/vulkan_core.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>

#pragma once

namespace nvvk {

// Simple container of the various physical device features and properties.
struct PhysicalDeviceInfo
{
  VkPhysicalDeviceProperties         properties10;
  VkPhysicalDeviceVulkan11Properties properties11 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES};
  VkPhysicalDeviceVulkan12Properties properties12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES};
  VkPhysicalDeviceVulkan13Properties properties13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES};
  VkPhysicalDeviceVulkan14Properties properties14 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_PROPERTIES};

  VkPhysicalDeviceFeatures         features10;
  VkPhysicalDeviceVulkan11Features features11 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
  VkPhysicalDeviceVulkan12Features features12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
  VkPhysicalDeviceVulkan13Features features13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
  VkPhysicalDeviceVulkan14Features features14 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES};

  std::vector<VkExtensionProperties> extensions;

  void init(VkPhysicalDevice physicalDevice, uint32_t apiVersion = VK_API_VERSION_1_4);

  bool hasDeviceExtension(const char* name) const
  {
    return std::any_of(extensions.begin(), extensions.end(),
                       [&](const VkExtensionProperties& ext) {
                         return strcmp(name, ext.extensionName) == 0;
                       });
  }
};

}  // namespace nvvk
