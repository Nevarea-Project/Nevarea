#include "VulkanDevice.hpp"
#include "VulkanSpec.hpp"
#include "VulkanDebug.hpp"
#include <cstdint>

namespace Nevarea::Renderer {
	QueueFamilyIndices find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface) {
		QueueFamilyIndices indices;

		u32 queue_family_count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, nullptr);

		std::vector<VkQueueFamilyProperties> families(queue_family_count);
		vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, families.data());

		for (u32 i = 0; i < queue_family_count; i++) {
		    VkQueueFlags flags = families[i].queueFlags;

			if ((flags & VK_QUEUE_GRAPHICS_BIT) && !indices.graphics_family.has_value())
			    indices.graphics_family = i;

			VkBool32 present_support = VK_FALSE;
			vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present_support);
			if (present_support && !indices.present_family.has_value())
			    indices.present_family = i;

			if (flags & VK_QUEUE_COMPUTE_BIT) {
			    if (!(flags & VK_QUEUE_GRAPHICS_BIT))
					indices.compute_family = i;
			    else if (!indices.compute_family.has_value())
					indices.compute_family = i;
			}

			if (flags & VK_QUEUE_TRANSFER_BIT) {
			    if (!(flags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)))
					indices.transfer_family = i;
			    else if (!indices.transfer_family.has_value())
					indices.transfer_family = i;
			}
		}

		if (indices.graphics_family.has_value()) {
		    VkBool32 gfx_present = VK_FALSE;
			vkGetPhysicalDeviceSurfaceSupportKHR(device, indices.graphics_family.value(), surface, &gfx_present);
			if (gfx_present) indices.present_family = indices.graphics_family;
		}

		if (!indices.compute_family.has_value())  indices.compute_family  = indices.graphics_family;
		if (!indices.transfer_family.has_value()) indices.transfer_family = indices.graphics_family;

		return indices;
	}

	static std::vector<std::string> get_available_extensions(VkPhysicalDevice device) {
		u32 extension_count = 0;
		VK_CHECK(vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, nullptr));

		std::vector<VkExtensionProperties> extensions(extension_count);
		VK_CHECK(vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, extensions.data()));

		std::vector<std::string> available;
		for (const auto& extension : extensions)
			available.push_back(extension.extensionName);

		return available;
	}

	static bool check_required_extensions_supported(VkPhysicalDevice device) {
        std::vector<std::string> available = get_available_extensions(device);

        for (const char* required : required_device_extensions)
            if (std::find(available.begin(), available.end(), required) == available.end()) return false;

        return true;
    }

	static bool is_swapchain_adequate(VkPhysicalDevice device, VkSurfaceKHR surface)
	{
		u32 format_count = 0;
		if (vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, nullptr) != VK_SUCCESS || format_count == 0)
			return false;

		u32 present_mode_count = 0;
		if (vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &present_mode_count, nullptr) != VK_SUCCESS || present_mode_count == 0)
			return false;

		VkSurfaceCapabilitiesKHR capabilities;
		if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &capabilities) != VK_SUCCESS)
			return false;

		if (capabilities.maxImageCount > 0 && capabilities.minImageCount > capabilities.maxImageCount)
			return false;

		return true;
	}

	static bool is_device_compatible(VkPhysicalDevice device, VkSurfaceKHR surface) {
		VkPhysicalDeviceProperties device_properties;
		vkGetPhysicalDeviceProperties(device, &device_properties);

		bool extensions_supported = check_required_extensions_supported(device);
		bool swapchain_adequate = is_swapchain_adequate(device, surface);
		bool queue_family_is_complete = find_queue_families(device, surface).is_complete();

		VkPhysicalDeviceVulkan12Features features12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
		VkPhysicalDeviceVulkan13Features features13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, &features12 };
		VkPhysicalDeviceFeatures2 device_features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &features13 };

		vkGetPhysicalDeviceFeatures2(device, &device_features);

		return queue_family_is_complete
			&& extensions_supported
			&& swapchain_adequate

			&& features13.dynamicRendering
			&& features13.synchronization2

			&& features12.bufferDeviceAddress
			&& features12.scalarBlockLayout
			&& features12.timelineSemaphore
			&& features12.runtimeDescriptorArray
			&& features12.descriptorBindingPartiallyBound
			&& features12.shaderStorageBufferArrayNonUniformIndexing
			&& features12.shaderSampledImageArrayNonUniformIndexing
			&& features12.shaderStorageImageArrayNonUniformIndexing
			&& features12.descriptorBindingStorageBufferUpdateAfterBind
			&& features12.descriptorBindingSampledImageUpdateAfterBind
			&& features12.descriptorBindingStorageImageUpdateAfterBind

			&& device_features.features.samplerAnisotropy
			&& device_features.features.vertexPipelineStoresAndAtomics
			&& device_features.features.fragmentStoresAndAtomics
			&& device_features.features.shaderInt64
		    && device_features.features.textureCompressionBC;
	}

	static void query_capabilities(DeviceContext& device_context) {
        std::vector<std::string> available = get_available_extensions(device_context.physical_device);
        auto has = [&](const char* name) {
            for (const std::string& extension : available) if (extension == name) return true;
            return false;
        };

        auto add = [&](const char* name) {
            for (const char* extension : device_context.enabled_extensions) if (strcmp(extension, name) == 0) return;
            device_context.enabled_extensions.push_back(name);
        };

		device_context.enabled_extensions.clear();
		for (const char* extension : required_device_extensions) add(extension);
		for (const char* extension : optional_device_extensions) if (has(extension)) add(extension);
		for (const char* extension : device_context.requested_extensions) if (has(extension)) add(extension);

		device_context.capabilities = {};
		device_context.capabilities.memory_priority = has(VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME);
		device_context.capabilities.pageable_memory = has(VK_EXT_PAGEABLE_DEVICE_LOCAL_MEMORY_EXTENSION_NAME);
		device_context.capabilities.present_id = has(VK_KHR_PRESENT_ID_EXTENSION_NAME);
		device_context.capabilities.present_wait = has(VK_KHR_PRESENT_WAIT_EXTENSION_NAME);
		device_context.capabilities.present_id2  = has(VK_KHR_PRESENT_ID_2_EXTENSION_NAME);
		device_context.capabilities.present_wait2 = has(VK_KHR_PRESENT_WAIT_2_EXTENSION_NAME);
		device_context.capabilities.swapchain_maintenance1 = has(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
		device_context.capabilities.descriptor_heap = has(VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME);
		device_context.capabilities.mutable_descriptor_type = has(VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME);
		device_context.capabilities.shader_object = has(VK_EXT_SHADER_OBJECT_EXTENSION_NAME);
		device_context.capabilities.extended_dynamic_state3 = has(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
		device_context.capabilities.shader_module_identifier = has(VK_EXT_SHADER_MODULE_IDENTIFIER_EXTENSION_NAME);
	}

	static u32 score_device(VkPhysicalDevice device) {
		VkPhysicalDeviceProperties device_properties;
		vkGetPhysicalDeviceProperties(device, &device_properties);

		VkPhysicalDeviceMemoryProperties memory_properties;
		vkGetPhysicalDeviceMemoryProperties(device, &memory_properties);

		u32 score = 0;

		switch (device_properties.deviceType) {
			case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: score += 10000; break;
			case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: score += 1000; break;
			case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: score += 500; break;
			case VK_PHYSICAL_DEVICE_TYPE_CPU: score += 10; break;
			default: break;
		}

		VkDeviceSize vram = 0;

		for (u32 i = 0; i < memory_properties.memoryHeapCount; ++i) {
			if (memory_properties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
				vram += memory_properties.memoryHeaps[i].size;
		}

		score += device_properties.limits.maxImageDimension2D;
		score += static_cast<u32>(vram / (1024 * 1024));

		return score;
	}

	static VkPhysicalDevice pick_best_compatible_device(std::vector<VkPhysicalDevice> physical_devices, VkSurfaceKHR surface)
	{
		VkPhysicalDevice best_device = VK_NULL_HANDLE;
		u32 best_score = 0;

		for (VkPhysicalDevice device : physical_devices) {
			if (!is_device_compatible(device, surface)) continue;

			u32 score = score_device(device);
			if (score > best_score) {
				best_score = score;
				best_device = device;
			}
		}

		NEVAREA_ASSERT(best_device != VK_NULL_HANDLE, "VULKAN DEVICE", "No device was found to be compatible!");
		return best_device;
	}

	NvResult vulkan_device_init(DeviceContext& device_context, VkInstance instance, VkSurfaceKHR surface)
	{
		vulkan_device_pick_physical_device(instance, surface, &device_context);
        NV_VALIDATE(device_context.physical_device != VK_NULL_HANDLE, NvResult::FEATURE_NOT_PRESENT, "no compatible physical device found");
		query_capabilities(device_context);
		vulkan_device_create_logical_device(surface, &device_context);

		return NvResult::SUCCESS;
	}

	void vulkan_device_destroy(DeviceContext* device_context)
	{
		vkDestroyDevice(device_context->device, nullptr);
	}

	void vulkan_device_pick_physical_device(VkInstance instance, VkSurfaceKHR surface, DeviceContext* device_context)
	{
		u32 physical_device_count = 0;
		VK_CHECK(vkEnumeratePhysicalDevices(instance, &physical_device_count, nullptr));

		std::vector<VkPhysicalDevice> physical_devices(physical_device_count);
		VK_CHECK(vkEnumeratePhysicalDevices(instance, &physical_device_count, physical_devices.data()));

		device_context->physical_device = pick_best_compatible_device(physical_devices, surface);

		VkPhysicalDeviceProperties device_properties;
		VkPhysicalDeviceFeatures device_features;
		vkGetPhysicalDeviceProperties(device_context->physical_device, &device_properties);
		vkGetPhysicalDeviceFeatures(device_context->physical_device, &device_features);

		NEVAREA_LOG(LogLevel::INFO, "Physical Device Chosen: %s", device_properties.deviceName);
	}

	NvResult vulkan_device_create_logical_device(VkSurfaceKHR surface, DeviceContext* device_context)
	{
		QueueFamilyIndices indices = find_queue_families(device_context->physical_device, surface);

		std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
		std::set<u32> unique_queue_families = {
			indices.graphics_family.value(),
			indices.present_family.value(),
			indices.compute_family.value(),
			indices.transfer_family.value()
		};

		float queue_priority = 1.0f;
		for (u32 queue_family : unique_queue_families) {
			VkDeviceQueueCreateInfo queue_create_info{};
			queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queue_create_info.queueFamilyIndex = queue_family;
			queue_create_info.queueCount = 1;
			queue_create_info.pQueuePriorities = &queue_priority;

			queue_create_infos.push_back(queue_create_info);
		}

		VkPhysicalDeviceVulkan12Features features12{};
		features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
		features12.bufferDeviceAddress = VK_TRUE;
		features12.scalarBlockLayout = VK_TRUE;
		features12.timelineSemaphore = VK_TRUE;
		features12.runtimeDescriptorArray = VK_TRUE;
		features12.descriptorBindingPartiallyBound = VK_TRUE;
		features12.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
		features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
		features12.shaderStorageImageArrayNonUniformIndexing = VK_TRUE;
		features12.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
		features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
		features12.descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;

		VkPhysicalDeviceVulkan13Features features13{};
		features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
		features13.dynamicRendering = VK_TRUE;
		features13.synchronization2 = VK_TRUE;
		features13.pNext = &features12;

		VkPhysicalDeviceFeatures2 features2{};
		features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		features2.features.samplerAnisotropy = VK_TRUE;
		features2.features.vertexPipelineStoresAndAtomics = VK_TRUE;
		features2.features.fragmentStoresAndAtomics = VK_TRUE;
		features2.features.shaderInt64 = VK_TRUE;
		features2.features.textureCompressionBC = VK_TRUE;
		features2.pNext = &features13;

		// holy shit
		VkPhysicalDeviceShaderModuleIdentifierFeaturesEXT smi{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MODULE_IDENTIFIER_FEATURES_EXT };
		VkPhysicalDevicePageableDeviceLocalMemoryFeaturesEXT pageable{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PAGEABLE_DEVICE_LOCAL_MEMORY_FEATURES_EXT };
		VkPhysicalDeviceMemoryPriorityFeaturesEXT mem_prio{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PRIORITY_FEATURES_EXT };
		VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT sm1{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT };
		VkPhysicalDevicePresentWaitFeaturesKHR present_wait{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR };
		VkPhysicalDevicePresentIdFeaturesKHR present_id{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR };
		VkPhysicalDeviceExtendedDynamicState3FeaturesEXT dyn_state3{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT };
		VkPhysicalDeviceShaderObjectFeaturesEXT shader_obj{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT };
		VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT mutable_desc{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT };
		VkPhysicalDeviceDescriptorHeapFeaturesEXT desc_heap{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT };

		void* opt_head = nullptr;

		if (device_context->capabilities.shader_module_identifier) {
			smi.shaderModuleIdentifier = VK_TRUE;
			smi.pNext = opt_head;
			opt_head = &smi;
		}
		if (device_context->capabilities.pageable_memory) {
			pageable.pageableDeviceLocalMemory = VK_TRUE;
			pageable.pNext = opt_head;
			opt_head = &pageable;
		}
		if (device_context->capabilities.memory_priority) {
			mem_prio.memoryPriority = VK_TRUE;
			mem_prio.pNext = opt_head;
			opt_head = &mem_prio;
		}
		if (device_context->capabilities.swapchain_maintenance1) {
			sm1.swapchainMaintenance1 = VK_TRUE;
			sm1.pNext = opt_head;
			opt_head = &sm1;
		}
		if (device_context->capabilities.present_wait) {
			present_wait.presentWait = VK_TRUE;
			present_wait.pNext = opt_head;
			opt_head = &present_wait;
		}
		if (device_context->capabilities.present_id) {
			present_id.presentId = VK_TRUE;
			present_id.pNext = opt_head;
			opt_head = &present_id;
		}
		if (device_context->capabilities.extended_dynamic_state3) {
			dyn_state3.extendedDynamicState3ColorBlendEnable = VK_TRUE;
			dyn_state3.extendedDynamicState3ColorBlendEquation = VK_TRUE;
			dyn_state3.extendedDynamicState3ColorWriteMask = VK_TRUE;
			dyn_state3.pNext = opt_head;
			opt_head = &dyn_state3;
		}
		if (device_context->capabilities.shader_object) {
			shader_obj.shaderObject = VK_TRUE;
			shader_obj.pNext = opt_head;
			opt_head = &shader_obj;
		}
		if (device_context->capabilities.mutable_descriptor_type) {
			mutable_desc.mutableDescriptorType = VK_TRUE;
			mutable_desc.pNext = opt_head;
			opt_head = &mutable_desc;
		}
		if (device_context->capabilities.descriptor_heap) {
			desc_heap.descriptorHeap = VK_TRUE;
			desc_heap.pNext = opt_head;
			opt_head = &desc_heap;
		}
		features12.pNext = opt_head;

		VkDeviceCreateInfo create_info{};
		create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		create_info.pNext = &features2;
		create_info.queueCreateInfoCount = static_cast<u32>(queue_create_infos.size());
		create_info.pQueueCreateInfos = queue_create_infos.data();
		create_info.pEnabledFeatures = nullptr;
		create_info.enabledExtensionCount   = (u32)device_context->enabled_extensions.size();
		create_info.ppEnabledExtensionNames = device_context->enabled_extensions.data();
		create_info.enabledLayerCount = 0;
		create_info.ppEnabledLayerNames = nullptr;

        VkBaseOutStructure* tail = reinterpret_cast<VkBaseOutStructure*>(&features2);
		if (device_context->user_feature_chain) {
            while (tail->pNext) tail = tail->pNext;
            tail->pNext = (VkBaseOutStructure*)device_context->user_feature_chain;
        }

		VkResult result = vkCreateDevice(device_context->physical_device, &create_info, nullptr, &device_context->device);
		if (result != VK_SUCCESS && device_context->user_feature_chain) {
		    NEVAREA_LOG(LogLevel::WARN, "device creation failed with requested feature chain (%s); retrying WITHOUT caller-requested features", string_VkResult(result));
            tail->pNext = nullptr;
            result = vkCreateDevice(device_context->physical_device, &create_info, nullptr, &device_context->device);
        }

		NV_CHECK_VK(result);

		device_context->graphics_family_index = indices.graphics_family.value();
		device_context->compute_family_index = indices.compute_family.value();
		device_context->transfer_family_index = indices.transfer_family.value();
		device_context->present_family_index = indices.present_family.value();

		vkGetDeviceQueue(device_context->device, indices.graphics_family.value(), 0, &device_context->graphics_queue);
		vkGetDeviceQueue(device_context->device, indices.present_family.value(), 0, &device_context->present_queue);
		vkGetDeviceQueue(device_context->device, indices.compute_family.value(), 0, &device_context->compute_queue);
		vkGetDeviceQueue(device_context->device, indices.transfer_family.value(), 0, &device_context->transfer_queue);

		VK_NAME(device_context->device, VK_OBJECT_TYPE_DEVICE, device_context->device, "nevarea_device");
		VK_NAME(device_context->device, VK_OBJECT_TYPE_QUEUE, device_context->graphics_queue, "graphics_queue");
		VK_NAME(device_context->device, VK_OBJECT_TYPE_QUEUE, device_context->present_queue, "present_queue");
		VK_NAME(device_context->device, VK_OBJECT_TYPE_QUEUE, device_context->compute_queue, "compute_queue");
		VK_NAME(device_context->device, VK_OBJECT_TYPE_QUEUE, device_context->transfer_queue, "transfer_queue");

		return NvResult::SUCCESS;
	}
}
