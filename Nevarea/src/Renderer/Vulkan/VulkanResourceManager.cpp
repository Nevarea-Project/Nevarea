#include "VulkanResourceManager.hpp"
#include "Core/n_pch.hpp"
#include "Renderer/Vulkan/VulkanFrames.hpp"
#include "VulkanDebug.hpp"
#include "VulkanTranslate.hpp"

#include "lib/Core.hpp"
#include "lib/Rendering.hpp"

namespace Nevarea::Renderer {
	static_assert(sizeof(k_format_info) / sizeof(FormatInfo) == (size_t)Format::COUNT,
		"k_format_info is out of sync with the public Format enum");

	static const FormatInfo& format_info(Format format) {
		return k_format_info[static_cast<uint32_t>(format)];
	}

	// TODO: place this in vulkantranslate
	VkFormat to_vk_format(Format format) {
		return format_info(format).vk;
	}

	static size_t format_size(Format format, uint32_t width, uint32_t height) {
		const FormatInfo& info = format_info(format);
		uint32_t bw = (width  + info.block_width  - 1) / info.block_width;
		uint32_t bh = (height + info.block_height - 1) / info.block_height;
		return static_cast<size_t>(bw) * bh * info.block_bytes;
	}

	static void query_acceleration_structure_properties(ResourceManager& manager) {
	    VkPhysicalDeviceAccelerationStructurePropertiesKHR acceleration_structure_properties{};
		acceleration_structure_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;

		VkPhysicalDeviceProperties2 device_properties{};
		device_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		device_properties.pNext = &acceleration_structure_properties;

		vkGetPhysicalDeviceProperties2(manager.physical_device, &device_properties);
		manager.scratch_alignment = acceleration_structure_properties.minAccelerationStructureScratchOffsetAlignment;
	}

	static const BindlessSlot k_bindless_slots[] = {
        { 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, NEVAREA_BUFFER_IMAGE_SIZE, nullptr, true },
        { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, NEVAREA_BUFFER_IMAGE_SIZE, nullptr, true },
        { 2, VK_DESCRIPTOR_TYPE_SAMPLER, NEVAREA_SAMPLER_SIZE, nullptr, true },
        { 3, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, NEVAREA_ACCEL_SIZE, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, false },
    };

	static bool extension_enabled(const ResourceManager& manager, const char* name) {
        if (!name) return true;

        for (const char* extension : manager.enabled_extensions)
            if (strcmp(extension, name) == 0) return true;

        return false;
    }

    static bool slot_active(const BindlessSlot& slot, const ResourceManager& manager) {
        return extension_enabled(manager, slot.required_extension);
    }

	void vulkan_create_descriptor_pool(ResourceManager& manager) {
	    std::vector<VkDescriptorPoolSize> pool_sizes;

		for (const BindlessSlot& slot : k_bindless_slots)
            if (slot_active(slot, manager)) pool_sizes.push_back({ slot.type, slot.count });

        VkDescriptorPoolCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        create_info.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        create_info.maxSets = 1;
        create_info.poolSizeCount = (uint32_t)pool_sizes.size();
        create_info.pPoolSizes = pool_sizes.data();

		VK_ASSERT(vkCreateDescriptorPool(manager.device, &create_info, nullptr, &manager.descriptor_pool));
		VK_NAME(manager.device, VK_OBJECT_TYPE_DESCRIPTOR_POOL, manager.descriptor_pool, "descriptor_pool");
	}

	void vulkan_create_descriptor_layout(ResourceManager& manager) {
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        std::vector<VkDescriptorBindingFlags> flags;

        for (const BindlessSlot& slot : k_bindless_slots) {
            if (!slot_active(slot, manager)) continue;

            bindings.push_back({ slot.binding, slot.type, slot.count, VK_SHADER_STAGE_ALL, nullptr });
            VkDescriptorBindingFlags flag = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
            if (slot.update_after_bind) flag |= VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
            flags.push_back(flag);
        }

        VkDescriptorSetLayoutBindingFlagsCreateInfo layout_flags{};
        layout_flags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
        layout_flags.bindingCount = (uint32_t)flags.size();
        layout_flags.pBindingFlags = flags.data();

        VkDescriptorSetLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_info.pNext = &layout_flags;
        layout_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
        layout_info.bindingCount = (uint32_t)bindings.size();
        layout_info.pBindings = bindings.data();

		VK_ASSERT(vkCreateDescriptorSetLayout(manager.device, &layout_info, nullptr, &manager.descriptor_layout));
		VK_NAME(manager.device, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, manager.descriptor_layout, "descriptor_set_layout");
	}

	void vulkan_init_descriptor_set(ResourceManager& manager) {
		VkDescriptorSetAllocateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		info.descriptorPool = manager.descriptor_pool;
		info.descriptorSetCount = 1;
		info.pSetLayouts = &manager.descriptor_layout;

		VK_ASSERT(vkAllocateDescriptorSets(manager.device, &info, &manager.descriptor_set));
		VK_NAME(manager.device, VK_OBJECT_TYPE_DESCRIPTOR_SET, manager.descriptor_set, "descriptor_set");
	}

	void vulkan_create_command_pool(ResourceManager& manager) {
	    VkCommandPoolCreateInfo create_info{};
		create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		create_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
		create_info.queueFamilyIndex = manager.graphics_family;
		VK_ASSERT(vkCreateCommandPool(manager.device, &create_info, nullptr, &manager.upload_pool));

        VkFenceCreateInfo fence_info{};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_info.flags = 0;
        VK_ASSERT(vkCreateFence(manager.device, &fence_info, nullptr, &manager.upload_fence));

        VkCommandBufferAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc.commandPool = manager.upload_pool;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;
        VK_ASSERT(vkAllocateCommandBuffers(manager.device, &alloc, &manager.upload_cmd));
	}

	void vulkan_init_timeline(ResourceManager& manager) {
        VkSemaphoreTypeCreateInfo type_info{};
        type_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        type_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        type_info.initialValue = 0;

        VkSemaphoreCreateInfo sem_info{};
        sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        sem_info.pNext = &type_info;
        VK_ASSERT(vkCreateSemaphore(manager.device, &sem_info, nullptr, &manager.transfer_timeline));

        manager.transfer_value = 0;

        VkCommandPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool_info.queueFamilyIndex = manager.transfer_family;
        VK_ASSERT(vkCreateCommandPool(manager.device, &pool_info, nullptr, &manager.transfer_pool));
	}

	void vulkan_resources_init(ResourceManager& manager, VmaAllocator allocator, const DeviceContext& device)
	{
		manager.allocator = allocator;
		manager.device = device.device;
		manager.physical_device = device.physical_device;
		manager.upload_queue = device.graphics_queue;
		manager.enabled_extensions = device.enabled_extensions;
		manager.transfer_queue = device.transfer_queue;
		manager.transfer_family = device.transfer_family_index;
		manager.graphics_family = device.graphics_family_index;

		query_acceleration_structure_properties(manager);
		vulkan_create_descriptor_pool(manager);
		vulkan_create_descriptor_layout(manager);
		vulkan_init_descriptor_set(manager);
		vulkan_create_command_pool(manager);
		vulkan_init_timeline(manager);
	}

	void vulkan_resources_destroy(ResourceManager& manager)
	{
    	vkQueueWaitIdle(manager.transfer_queue);

    	for (size_t i = 0; i < manager.buffers.data.size(); ++i) {
			if (manager.buffers.data[i].buffer != VK_NULL_HANDLE) {
				std::cerr << "[NEVAREA]: [RESOURCE MANAGER] Leaked buffer at slot " << i << std::endl;
				vmaDestroyBuffer(manager.allocator, manager.buffers.data[i].buffer, manager.buffers.data[i].allocation);
			}
		}

		manager.buffers.data.clear();
		manager.buffers.generations.clear();
		manager.buffers.free_list.clear();

		for (auto& img : manager.images.data) {
            if (img.image != VK_NULL_HANDLE) {
                std::cerr << "[NEVAREA]: [RESOURCE MANAGER] Leaked image" << std::endl;
                if (img.storage_view != VK_NULL_HANDLE) vkDestroyImageView(manager.device, img.storage_view, nullptr);
                vkDestroyImageView(manager.device, img.view, nullptr);
                vmaDestroyImage(manager.allocator, img.image, img.allocation);
            }
        }
        manager.images.data.clear();
        manager.images.generations.clear();
        manager.images.free_list.clear();

        for (size_t i = 0; i < manager.samplers.data.size(); ++i) {
            if (manager.samplers.data[i] != VK_NULL_HANDLE) {
                std::cerr << "[NEVAREA]: [RESOURCE MANAGER] Leaked sampler at slot " << i << std::endl;
                vkDestroySampler(manager.device, manager.samplers.data[i], nullptr);
            }
        }
        manager.samplers.data.clear();
        manager.samplers.generations.clear();
        manager.samplers.free_list.clear();

		vkDestroyFence(manager.device, manager.upload_fence, nullptr);
		vkDestroyCommandPool(manager.device, manager.upload_pool, nullptr);

        for (auto& upload : manager.pending_uploads)
            vmaDestroyBuffer(manager.allocator, upload.staging, upload.allocation);

        manager.pending_uploads.clear();
        vkDestroyCommandPool(manager.device, manager.transfer_pool, nullptr);
        vkDestroySemaphore(manager.device, manager.transfer_timeline, nullptr);

		vkDestroyDescriptorSetLayout(manager.device, manager.descriptor_layout, nullptr);
		vkDestroyDescriptorPool(manager.device, manager.descriptor_pool, nullptr);
	}

	AccelStructHandle vulkan_create_acceleration_structure(ResourceManager& manager, const AccelStructDescription& description) {
        NEVAREA_ASSERT(vkCreateAccelerationStructureKHR != nullptr, "RT",
            "VK_KHR_acceleration_structure is not enabled! request it before you call create_blas");

        std::vector<VkAccelerationStructureGeometryKHR> geometries(description.geometry_count);
        std::vector<uint32_t> prim_counts(description.geometry_count);

        for (uint32_t i = 0; i < description.geometry_count; i++) {
            const AccelGeometry& accel = description.geometries[i];
            VkAccelerationStructureGeometryKHR& accel_geometry = geometries[i];

            accel_geometry = {};
            accel_geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
            accel_geometry.geometryType = to_vk_geometry_type(accel.type);
            accel_geometry.flags = to_vk_geometry_flags(accel.flags);

            switch (accel.type) {
                case AccelGeometryType::TRIANGLES: {
                    auto& triangles = accel_geometry.geometry.triangles;
                    triangles = {};
                    triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
                    triangles.vertexFormat = to_vk_format(accel.vertex_format);
                    triangles.vertexData.deviceAddress = accel.vertex_address;
                    triangles.vertexStride = accel.vertex_stride;
                    triangles.maxVertex = accel.max_vertex;
                    triangles.indexType = to_vk_index_type(accel.index_type);
                    triangles.indexData.deviceAddress = accel.index_address;
                    triangles.transformData.deviceAddress = accel.transform_address;
                    break;
                }
                case AccelGeometryType::AABBS: {
                    auto& aabb = accel_geometry.geometry.aabbs;
                    aabb = {};
                    aabb.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
                    aabb.data.deviceAddress = accel.aabb_address;
                    aabb.stride = accel.aabb_stride;
                    break;
                }
                case AccelGeometryType::INSTANCES: {
                    auto& instance = accel_geometry.geometry.instances;
                    instance = {};
                    instance.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
                    instance.arrayOfPointers = accel.instances_array_of_pointers ? VK_TRUE : VK_FALSE;
                    instance.data.deviceAddress = accel.instances_address;
                    break;
                }
            }

            prim_counts[i] = accel.primitive_count;
        }

        VkAccelerationStructureBuildGeometryInfoKHR build{};
        build.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        build.type = to_vk_accel_type(description.type);
        build.mode = to_vk_build_mode(description.mode);
        build.flags = to_vk_accel_build_flags(description.flags);
        build.geometryCount = description.geometry_count;
        build.pGeometries = geometries.data();

        VkAccelerationStructureBuildSizesInfoKHR sizes{};
        sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        vkGetAccelerationStructureBuildSizesKHR(manager.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, prim_counts.data(), &sizes);

        BufferDescription buffer_description{};
        buffer_description.size = sizes.accelerationStructureSize;
        buffer_description.usage = BufferUsage::ACCEL_STORAGE;
        buffer_description.memory = MemoryLocation::GPU_ONLY;

        BufferHandle backing = vulkan_create_buffer(manager, buffer_description);

        VkAccelerationStructureKHR accel = VK_NULL_HANDLE;
        VkAccelerationStructureCreateInfoKHR create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        create_info.buffer = vulkan_get_buffer(manager, backing);
        create_info.size = sizes.accelerationStructureSize;
        create_info.type = to_vk_accel_type(description.type);
        VK_ASSERT(vkCreateAccelerationStructureKHR(manager.device, &create_info, nullptr, &accel));

        VkBuffer scratch_buffer;
        VmaAllocation scratch_allocation;

        VkBufferCreateInfo scratch_buffer_info{};
        scratch_buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        scratch_buffer_info.size = sizes.buildScratchSize;
        scratch_buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

        VmaAllocationCreateInfo scratch_allocation_info{};
        scratch_allocation_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        VK_ASSERT(vmaCreateBufferWithAlignment(manager.allocator, &scratch_buffer_info, &scratch_allocation_info,
             manager.scratch_alignment, &scratch_buffer, &scratch_allocation, nullptr));

        VkBufferDeviceAddressInfo scratch_address_device{};
        scratch_address_device.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        scratch_address_device.buffer = scratch_buffer;
        build.scratchData.deviceAddress = vkGetBufferDeviceAddress(manager.device, &scratch_address_device);
        build.dstAccelerationStructure = accel;

        std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges(description.geometry_count);
        for (uint32_t i = 0; i < description.geometry_count; i++) {
            const AccelGeometry& geometry = description.geometries[i];
            ranges[i] = { geometry.primitive_count, geometry.primitive_offset, geometry.first_vertex, geometry.transform_offset };
        }

        const VkAccelerationStructureBuildRangeInfoKHR* pranges = ranges.data();
        vulkan_immediate_submit(manager, [&](VkCommandBuffer cmd) {
            vkCmdBuildAccelerationStructuresKHR(cmd, 1, &build, &pranges);
        });
        vmaDestroyBuffer(manager.allocator, scratch_buffer, scratch_allocation);

        VkAccelerationStructureDeviceAddressInfoKHR accel_device_info{};
        accel_device_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        accel_device_info.accelerationStructure = accel;
        uint64_t address = vkGetAccelerationStructureDeviceAddressKHR(manager.device, &accel_device_info);

        uint32_t index = manager.accels.add({ accel, backing, address });

        VkWriteDescriptorSetAccelerationStructureKHR accel_write{};
        accel_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
        accel_write.accelerationStructureCount = 1;
        accel_write.pAccelerationStructures = &accel;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.pNext = &accel_write;
        write.dstSet = manager.descriptor_set;
        write.dstBinding = 3;
        write.dstArrayElement = index;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        vkUpdateDescriptorSets(manager.device, 1, &write, 0, nullptr);

        return { index, manager.accels.generations[index] };
	}

	uint64_t vulkan_get_accel_address(ResourceManager &manager, AccelStructHandle handle) {
	    return manager.accels.get(handle.index, handle.generation).address;
	}

	void vulkan_destroy_acceleration_structure(ResourceManager &manager, AccelStructHandle handle, FrameContext &frame) {
        AccelStructData& accel_data = manager.accels.get(handle.index, handle.generation);
        VkAccelerationStructureKHR accel = accel_data.accel;
        VkDevice device = manager.device;

        vulkan_resources_push_deletor(frame.deletion_queues[frame.current_frame],
            [device, accel]() { vkDestroyAccelerationStructureKHR(device, accel, nullptr); });

        vulkan_destroy_buffer(manager, accel_data.backing, frame);
        manager.accels.remove(handle.index);
	}

	void vulkan_immediate_submit(ResourceManager &manager, std::function<void (VkCommandBuffer)> &&record) {
	    vkResetFences(manager.device, 1, &manager.upload_fence);
	    vkResetCommandPool(manager.device, manager.upload_pool, 0);

		VkCommandBufferBeginInfo begin{};
		begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_ASSERT(vkBeginCommandBuffer(manager.upload_cmd, &begin));

        record(manager.upload_cmd);
        VK_ASSERT(vkEndCommandBuffer(manager.upload_cmd));

        VkCommandBufferSubmitInfo cmd_info{};
        cmd_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        cmd_info.commandBuffer = manager.upload_cmd;

        VkSubmitInfo2 submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &cmd_info;

        VK_ASSERT(vkQueueSubmit2(manager.upload_queue, 1, &submit, manager.upload_fence));
        VK_ASSERT(vkWaitForFences(manager.device, 1, &manager.upload_fence, VK_TRUE, UINT64_MAX));
	}

	void vulkan_resources_push_deletor(DeletionQueue& deletion_queue, std::function<void()>&& fn)
	{
		deletion_queue.deletors.push_back(std::move(fn));
	}

	void vulkan_resources_flush_deletors(DeletionQueue& deletion_queue)
	{
		for (auto& fn : deletion_queue.deletors) fn();
		deletion_queue.deletors.clear();
	}

	BufferHandle vulkan_create_buffer(ResourceManager& manager, const BufferDescription& buffer_description)
	{
		VkBufferCreateInfo buffer_create_info{};
		buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buffer_create_info.size = buffer_description.size;
		buffer_create_info.usage = to_vk_buffer_usage(buffer_description.usage) | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

		uint32_t families[2] = { manager.graphics_family, manager.transfer_family };
        if (manager.graphics_family != manager.transfer_family) {
            buffer_create_info.sharingMode = VK_SHARING_MODE_CONCURRENT;
            buffer_create_info.queueFamilyIndexCount = 2;
            buffer_create_info.pQueueFamilyIndices = families;
        } else {
            buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

		VmaAllocationCreateInfo allocation_create_info{};
		allocation_create_info.usage = to_vma_memory(buffer_description.memory);
		allocation_create_info.priority = buffer_description.priority;

		VkBuffer buffer = VK_NULL_HANDLE;
		VmaAllocation allocation = VK_NULL_HANDLE;

		VK_ASSERT(vmaCreateBuffer(manager.allocator, &buffer_create_info, &allocation_create_info, &buffer, &allocation, nullptr));
		VK_NAME(manager.device, VK_OBJECT_TYPE_BUFFER, buffer, buffer_description.debug_name);

		VkBufferDeviceAddressInfo address_info{};
        address_info.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        address_info.buffer = buffer;
        uint64_t address = vkGetBufferDeviceAddress(manager.device, &address_info);

        BufferData buffer_data = { buffer, allocation, address };
		uint32_t index = manager.buffers.add(buffer_data);

		return { index, manager.buffers.generations[index] };
	}

	VkBuffer vulkan_get_buffer(const ResourceManager& manager, BufferHandle handle)
	{
		return manager.buffers.get(handle.index, handle.generation).buffer;
	}

	uint64_t vulkan_get_buffer_address(const ResourceManager& manager, BufferHandle handle) {
	    return manager.buffers.get(handle.index, handle.generation).address;
	}

	static TransferSubmit submit_transfer(ResourceManager& manager, std::function<void(VkCommandBuffer)>&& record) {
	    VkCommandBuffer cmd;

	    if (!manager.free_transfer_cmds.empty()) {
            cmd = manager.free_transfer_cmds.back(); manager.free_transfer_cmds.pop_back();
            vkResetCommandBuffer(cmd, 0);
        } else {
            VkCommandBufferAllocateInfo allocate_info{};
            allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocate_info.commandPool = manager.transfer_pool;
            allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocate_info.commandBufferCount = 1;
            VK_ASSERT(vkAllocateCommandBuffers(manager.device, &allocate_info, &cmd));
        }

		VkCommandBufferBeginInfo begin_info{};
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        VK_ASSERT(vkBeginCommandBuffer(cmd, &begin_info));
        record(cmd);
        VK_ASSERT(vkEndCommandBuffer(cmd));

        uint64_t value = ++manager.transfer_value;

        VkCommandBufferSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        submit_info.commandBuffer = cmd;

        VkSemaphoreSubmitInfo signal{};
        signal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        signal.semaphore = manager.transfer_timeline;
        signal.value = value;
        signal.stageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;

        VkSubmitInfo2 submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &submit_info;
        submit.signalSemaphoreInfoCount = 1;
        submit.pSignalSemaphoreInfos = &signal;

        VK_ASSERT(vkQueueSubmit2(manager.transfer_queue, 1, &submit, VK_NULL_HANDLE));
        return { value, cmd };
	}

	void vulkan_upload_buffer(ResourceManager& manager, BufferHandle dst, const void* data, size_t size) {
	    VkBuffer dst_buffer = vulkan_get_buffer(manager, dst);

		VkBufferCreateInfo stg_info{};
		stg_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		stg_info.size = size;
		stg_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		stg_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VmaAllocationCreateInfo alloc_info{};
		alloc_info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

		VkBuffer staging_buffer;
		VmaAllocation staging_alloc;
		VK_ASSERT(vmaCreateBuffer(manager.allocator, &stg_info, &alloc_info, &staging_buffer, &staging_alloc, nullptr));

        void* mapped;
        VK_ASSERT(vmaMapMemory(manager.allocator, staging_alloc, &mapped));
        memcpy(mapped, data, size);
        vmaUnmapMemory(manager.allocator, staging_alloc);

        /*vulkan_immediate_submit(manager, [&](VkCommandBuffer cmd) {
            VkBufferCopy region{ 0, 0, size };
            vkCmdCopyBuffer(cmd, staging_buffer, dst_buffer, 1, &region);
            });*/

        //vmaDestroyBuffer(manager.allocator, staging_buffer, staging_alloc);

        TransferSubmit sub = submit_transfer(manager, [=](VkCommandBuffer cmd) {
            VkBufferCopy region{ 0, 0, size };
            vkCmdCopyBuffer(cmd, staging_buffer, dst_buffer, 1, &region);
        });
        manager.pending_uploads.push_back({ staging_buffer, staging_alloc, sub.cmd, sub.value });
	}

	void vulkan_destroy_buffer(ResourceManager& manager, BufferHandle handle, FrameContext& frame) {
        BufferData& buffer_data = manager.buffers.get(handle.index, handle.generation);
        VkBuffer buffer = buffer_data.buffer;
        VmaAllocation allocation = buffer_data.allocation;
        VmaAllocator allocator = manager.allocator;

        vulkan_resources_push_deletor(frame.deletion_queues[frame.current_frame],
            [allocator, buffer, allocation]() { vmaDestroyBuffer(allocator, buffer, allocation); });

        manager.buffers.remove(handle.index);
    }

	ImageHandle vulkan_create_image(ResourceManager& manager, const ImageDescription& description)
	{
	    VkExtent2D extent = { description.width, description.height };
	    VkFormat format = k_format_info[(uint32_t)description.format].vk;
	    VkImageUsageFlags usage = to_vk_image_usage(description.usage);

		AllocatedImage img{};
		img.extent = extent;
		img.format = description.format;
		img.usage = usage;

		uint32_t mips = description.mip_levels;
		if (mips == 0) {
	        uint32_t m = std::max<uint32_t>(extent.width, extent.height);
			mips = 1;

		    while (m > 1) {
				m >>= 1;
			    mips++;
			}
	    }

		VkImageCreateInfo image_info{};
		image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		image_info.imageType = to_vk_image_type(description.image_type);
		image_info.format = format;
		image_info.extent = { extent.width, extent.height, description.depth };
		image_info.mipLevels = mips;
		image_info.arrayLayers = description.array_layers;
		image_info.samples = (VkSampleCountFlagBits)description.samples;
		image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
		image_info.usage = usage;
		image_info.flags = to_vk_image_create_flags(description.flags, description.image_type);

		uint32_t families[2] = { manager.graphics_family, manager.transfer_family };
		if (manager.graphics_family != manager.transfer_family) {
		    image_info.sharingMode = VK_SHARING_MODE_CONCURRENT;
			image_info.queueFamilyIndexCount = 2;
			image_info.pQueueFamilyIndices = families;
		}

		VmaAllocationCreateInfo alloc_info{};
		alloc_info.usage = to_vma_memory(description.memory_location);
		alloc_info.priority = description.priority;

		VK_ASSERT(vmaCreateImage(manager.allocator, &image_info, &alloc_info, &img.image, &img.allocation, nullptr));
		VK_NAME(manager.device, VK_OBJECT_TYPE_IMAGE, img.image, "nevarea_image");

		VkImageViewCreateInfo view_info{};
		view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		view_info.image = img.image;
		view_info.viewType = to_vk_image_view_type(description.image_type);
		view_info.format = format;
		view_info.subresourceRange = { k_format_info[(uint32_t)description.format].aspect, 0, mips, 0, description.array_layers };
		VK_ASSERT(vkCreateImageView(manager.device, &view_info, nullptr, &img.view));

		if (usage & VK_IMAGE_USAGE_STORAGE_BIT) {
            VkImageViewCreateInfo copy = view_info;
            copy.subresourceRange.baseMipLevel = 0;
            copy.subresourceRange.levelCount = 1;

            if (copy.viewType == VK_IMAGE_VIEW_TYPE_CUBE || copy.viewType == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY)
                copy.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;

            VK_ASSERT(vkCreateImageView(manager.device, &copy, nullptr, &img.storage_view));
        }

		uint32_t index = manager.images.add(img);

		if (usage & VK_IMAGE_USAGE_STORAGE_BIT) {
			VkDescriptorImageInfo desc_image{};
			desc_image.imageView = img.storage_view;
			desc_image.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

			VkWriteDescriptorSet write{};
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.dstSet = manager.descriptor_set;
			write.dstBinding = 1;
			write.dstArrayElement = index;
			write.descriptorCount = 1;
			write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			write.pImageInfo = &desc_image;
			vkUpdateDescriptorSets(manager.device, 1, &write, 0, nullptr);
		}

		if (usage & VK_IMAGE_USAGE_SAMPLED_BIT) {
            VkDescriptorImageInfo desc_image{};
            desc_image.imageView = img.view;
            desc_image.imageLayout = (usage & VK_IMAGE_USAGE_STORAGE_BIT) ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = manager.descriptor_set;
            write.dstBinding = 0;
            write.dstArrayElement = index;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            write.pImageInfo = &desc_image;
            vkUpdateDescriptorSets(manager.device, 1, &write, 0, nullptr);
        }

		if (usage & VK_IMAGE_USAGE_STORAGE_BIT) {
            vulkan_immediate_submit(manager, [&](VkCommandBuffer cmd) {
                transition_image(cmd, img.image,
                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
            });

            manager.images.data[index].current_layout = VK_IMAGE_LAYOUT_GENERAL;
        }

		return { index, manager.images.generations[index] };
	}

	AllocatedImage& vulkan_get_image(ResourceManager& manager, ImageHandle handle) {
		return manager.images.get(handle.index, handle.generation);
	}

	void vulkan_upload_image(ResourceManager& manager, ImageHandle handle, const void* pixels, size_t size) {
	    AllocatedImage& img = vulkan_get_image(manager, handle);

		NEVAREA_ASSERT((img.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0,
            "RESOURCE MANAGER", "upload_image: image must be created with ImageUsage::TRANSFER_DST!");

		size_t expected = format_size(img.format, img.extent.width, img.extent.height);

		NEVAREA_ASSERT(size >= expected, "RESOURCE MANAGER", "upload_image: pixel data smaller than image requires!");

		VkBufferCreateInfo buf_info{};
		buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buf_info.size = size;
        buf_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        buf_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo alloc_ci{};
        alloc_ci.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

        VkBuffer staging; VmaAllocation staging_alloc;
        VK_ASSERT(vmaCreateBuffer(manager.allocator, &buf_info, &alloc_ci, &staging, &staging_alloc, nullptr));

        void* data;
        VK_ASSERT(vmaMapMemory(manager.allocator, staging_alloc, &data));
        memcpy(data, pixels, size);
        vmaUnmapMemory(manager.allocator, staging_alloc);

        /*vulkan_immediate_submit(manager, [&](VkCommandBuffer cmd) {
            transition_image(cmd, img.image,
                img.current_layout,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

            VkBufferImageCopy region{};
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = k_format_info[(uint32_t)img.format].aspect;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = { 0, 0, 0 };
            region.imageExtent = { img.extent.width, img.extent.height, 1 };

            vkCmdCopyBufferToImage(cmd, staging, img.image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &region);

            transition_image(cmd, img.image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

            img.current_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        });

        vmaDestroyBuffer(manager.allocator, staging, staging_alloc);*/

        TransferSubmit sub = submit_transfer(manager, [=](VkCommandBuffer cmd) {
            transition_image(cmd, img.image,
                img.current_layout,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

            VkBufferImageCopy region{};
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = k_format_info[(uint32_t)img.format].aspect;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = { 0, 0, 0 };
            region.imageExtent = { img.extent.width, img.extent.height, 1 };

            vkCmdCopyBufferToImage(cmd, staging, img.image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &region);

            transition_image(cmd, img.image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0);
        });

        manager.pending_uploads.push_back({ staging, staging_alloc, sub.cmd, sub.value });
        img.current_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	void vulkan_destroy_image(ResourceManager& manager, ImageHandle handle, FrameContext& frame)
	{
		AllocatedImage& img = vulkan_get_image(manager, handle);

		VmaAllocator allocator = manager.allocator;
		VkDevice device = manager.device;
		VkImage image = img.image;
		VkImageView view = img.view;
		VkImageView storage_view = img.storage_view;
		VmaAllocation allocation = img.allocation;

		vulkan_resources_push_deletor(frame.deletion_queues[frame.current_frame], [allocator, device, image, view, allocation, storage_view]() {
		    if (storage_view != VK_NULL_HANDLE) vkDestroyImageView(device, storage_view, nullptr);
		    vkDestroyImageView(device, view, nullptr);
			vmaDestroyImage(allocator, image, allocation);
			});

		manager.images.remove(handle.index);
	}

	Sampler vulkan_create_sampler(ResourceManager& manager, const SamplerDescription& description) {
	    VkSamplerCreateInfo create_info{};
		create_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		create_info.pNext = nullptr;
		create_info.flags = 0;

		create_info.magFilter = to_vk_filter(description.mag_filter);
		create_info.minFilter = to_vk_filter(description.min_filter);

		create_info.addressModeU = to_vk_address_mode(description.address_u);
		create_info.addressModeV = to_vk_address_mode(description.address_v);
		create_info.addressModeW = to_vk_address_mode(description.address_w);

		create_info.anisotropyEnable = (description.max_anisotropy > 1);
		create_info.maxAnisotropy = description.max_anisotropy;

		create_info.borderColor = to_vk_border_color(description.border_color);

		create_info.unnormalizedCoordinates = VK_FALSE;

		create_info.compareEnable = (description.compare_op != CompareOp::NONE);
		create_info.compareOp = to_vk_compare_op(description.compare_op);

		create_info.mipmapMode = to_vk_mipmap_mode(description.mipmap_mode);
		create_info.mipLodBias = description.mip_lod_bias;
		create_info.minLod = description.min_lod;
		create_info.maxLod = description.max_lod >= 1000.0f ? VK_LOD_CLAMP_NONE : description.max_lod;

		VkSampler sampler;

		VK_ASSERT(vkCreateSampler(manager.device, &create_info, nullptr, &sampler));

		uint32_t index = manager.samplers.add(sampler);

		VkDescriptorImageInfo image_info{};
		image_info.sampler = sampler;

		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = manager.descriptor_set;
		write.dstBinding = 2;
		write.dstArrayElement = index;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
		write.pImageInfo = &image_info;

		vkUpdateDescriptorSets(manager.device, 1, &write, 0, nullptr);

		return { index, manager.samplers.generations[index] };
	}

	void vulkan_destroy_sampler(ResourceManager& manager, Sampler sampler, FrameContext& frame) {
    	VkDevice device = manager.device;

        VkSampler raw_sampler = manager.samplers.get(sampler.id, sampler.generation);

        vulkan_resources_push_deletor(frame.deletion_queues[frame.current_frame], [device, raw_sampler]() {
            vkDestroySampler(device, raw_sampler, nullptr);
        });

        manager.samplers.remove(sampler.id);
	}
}
