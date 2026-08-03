#pragma once

#include "Core/n_pch.hpp"
#include "lib/Rendering.hpp"

#include <vk_mem_alloc.h>
#include "VulkanSwapchain.hpp"

// For now its gonna be standalone, obviously this should be for every renderer but well.. its only vulkan for now so
namespace Nevarea::Renderer {
	#define NEVAREA_BUFFER_IMAGE_SIZE 1000
	#define NEVAREA_BUFFER_STORAGE_SIZE 1000
	#define NEVAREA_SAMPLER_SIZE 64
	#define NEVAREA_ACCEL_SIZE 64

	struct ImageHandle {
		u32 index = UINT32_MAX;
		u32 generation = 0;
		bool is_valid() const { return index != UINT32_MAX; }
	};

	struct AllocatedImage {
		VkImage image = VK_NULL_HANDLE;
		VkImageView view = VK_NULL_HANDLE;
		VkImageView storage_view = VK_NULL_HANDLE;
		VmaAllocation allocation = VK_NULL_HANDLE;
		VkExtent2D extent = {};
		Format format = Format::COUNT;
		VkImageLayout current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkImageUsageFlags usage = 0;
	};

	struct BufferHandle {
		u32 index = UINT32_MAX;
		u32 generation = 0;
		bool is_valid() const { return index != UINT32_MAX; }
	};

	struct AccelStructHandle {
        u32 index = UINT32_MAX;
        u32 generation = 0;
        bool is_valid() const { return index != UINT32_MAX; }
    };

	struct FormatInfo {
		VkFormat vk;
		u32 block_bytes;
		u32 block_width;
		u32 block_height;
		VkImageAspectFlags aspect;
	};

	static const FormatInfo k_format_info[] = {
	    { VK_FORMAT_R8_UNORM, 1, 1, 1, VK_IMAGE_ASPECT_COLOR_BIT },
		{ VK_FORMAT_R8G8_UNORM, 2, 1, 1, VK_IMAGE_ASPECT_COLOR_BIT },
		{ VK_FORMAT_R8G8B8A8_UNORM, 4, 1, 1, VK_IMAGE_ASPECT_COLOR_BIT },
		{ VK_FORMAT_R8G8B8A8_SRGB, 4, 1, 1, VK_IMAGE_ASPECT_COLOR_BIT },
		{ VK_FORMAT_B8G8R8A8_UNORM, 4, 1, 1, VK_IMAGE_ASPECT_COLOR_BIT },
		{ VK_FORMAT_B8G8R8A8_SRGB, 4, 1, 1, VK_IMAGE_ASPECT_COLOR_BIT },
		{ VK_FORMAT_R16_SFLOAT, 2, 1, 1, VK_IMAGE_ASPECT_COLOR_BIT },
		{ VK_FORMAT_R16G16_SFLOAT, 4, 1, 1, VK_IMAGE_ASPECT_COLOR_BIT },
		{ VK_FORMAT_R16G16B16A16_SFLOAT, 8, 1, 1, VK_IMAGE_ASPECT_COLOR_BIT },
		{ VK_FORMAT_R32_SFLOAT, 4, 1, 1, VK_IMAGE_ASPECT_COLOR_BIT },
		{ VK_FORMAT_R32G32_SFLOAT, 8, 1, 1, VK_IMAGE_ASPECT_COLOR_BIT },
		{ VK_FORMAT_R32G32B32_SFLOAT, 12, 1, 1, VK_IMAGE_ASPECT_COLOR_BIT },
		{ VK_FORMAT_R32G32B32A32_SFLOAT, 16, 1, 1, VK_IMAGE_ASPECT_COLOR_BIT },
		{ VK_FORMAT_A2B10G10R10_UNORM_PACK32, 4, 1, 1, VK_IMAGE_ASPECT_COLOR_BIT },
		{ VK_FORMAT_B10G11R11_UFLOAT_PACK32, 4, 1, 1, VK_IMAGE_ASPECT_COLOR_BIT },
		{ VK_FORMAT_R32_UINT, 4, 1, 1, VK_IMAGE_ASPECT_COLOR_BIT },
		{ VK_FORMAT_D16_UNORM, 2, 1, 1, VK_IMAGE_ASPECT_DEPTH_BIT },
		{ VK_FORMAT_D32_SFLOAT, 4, 1, 1, VK_IMAGE_ASPECT_DEPTH_BIT },
		{ VK_FORMAT_D24_UNORM_S8_UINT, 4, 1, 1, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT },
		{ VK_FORMAT_D32_SFLOAT_S8_UINT, 8, 1, 1, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT },
		{ VK_FORMAT_BC1_RGBA_UNORM_BLOCK, 8, 4, 4, VK_IMAGE_ASPECT_COLOR_BIT },
        { VK_FORMAT_BC1_RGBA_SRGB_BLOCK, 8, 4, 4, VK_IMAGE_ASPECT_COLOR_BIT },
        { VK_FORMAT_BC3_UNORM_BLOCK, 16, 4, 4, VK_IMAGE_ASPECT_COLOR_BIT },
        { VK_FORMAT_BC3_SRGB_BLOCK, 16, 4, 4, VK_IMAGE_ASPECT_COLOR_BIT },
        { VK_FORMAT_BC4_UNORM_BLOCK, 8, 4, 4, VK_IMAGE_ASPECT_COLOR_BIT },
        { VK_FORMAT_BC5_UNORM_BLOCK, 16, 4, 4, VK_IMAGE_ASPECT_COLOR_BIT },
        { VK_FORMAT_BC7_UNORM_BLOCK, 16, 4, 4, VK_IMAGE_ASPECT_COLOR_BIT },
        { VK_FORMAT_BC7_SRGB_BLOCK, 16, 4, 4, VK_IMAGE_ASPECT_COLOR_BIT },
	};

	template<class T>
	struct Pool {
	    std::vector<T> data;
		std::vector<u32> generations;
		std::vector<u32> free_list;

		u32 add(const T& item) {
            u32 index;
            if (!free_list.empty()) { index = free_list.back(); free_list.pop_back(); data[index] = item; }
            else { index = static_cast<u32>(data.size()); data.push_back(item); generations.push_back(0); }
            return index;
		}

        T& get(u32 index, [[maybe_unused]] u32 gen) {
            NEVAREA_ASSERT(index < data.size() && gen == generations[index], "POOL", "stale/oob handle");
            return data[index];
        }

        const T& get(u32 index, [[maybe_unused]] u32 gen) const {
			NEVAREA_ASSERT(index < data.size() && gen == generations[index], "POOL", "stale/oob handle");
			return data[index];
		}

        void remove(u32 index) {
            data[index] = T{};
            generations[index]++;
            free_list.push_back(index);
        }

        bool alive(u32 index, u32 gen) const { return index < data.size() && gen == generations[index]; }
	};

	struct BufferData {
        VkBuffer buffer;
        VmaAllocation allocation;
        uint64_t address;
	};

	struct AccelStructData {
	    VkAccelerationStructureKHR accel = VK_NULL_HANDLE;
		BufferHandle backing;
		uint64_t address;
	};

	struct BindlessSlot {
	    u32 binding;
		VkDescriptorType type;
		u32 count;
		const char* required_extension;
		bool update_after_bind;
	};

	struct PendingUpload {
	    VkBuffer staging;
		VmaAllocation allocation;
		VkCommandBuffer cmd;
		uint64_t value;
	};

	struct TransferSubmit {
	    uint64_t value;
		VkCommandBuffer cmd;
	};

	struct ResourceManager {
	    Pool<AccelStructData> accels;
    	Pool<BufferData> buffers;
        Pool<AllocatedImage> images;
        Pool<VkSampler> samplers;

        std::vector<const char*> enabled_extensions;

		VmaAllocator allocator;

		VkDescriptorSet descriptor_set;
		VkDescriptorSetLayout descriptor_layout;
		VkDescriptorPool descriptor_pool;

		VkCommandPool upload_pool;
		VkCommandBuffer upload_cmd;
		VkFence upload_fence;
		VkQueue upload_queue;

		VkQueue transfer_queue;
		u32 transfer_family;
		u32 graphics_family;

		VkCommandPool transfer_pool;
		VkSemaphore transfer_timeline;
		uint64_t transfer_value = 0;

		std::vector<PendingUpload> pending_uploads;
		std::vector<VkCommandBuffer> free_transfer_cmds;

		VkDevice device;
		VkPhysicalDevice physical_device;

		u32 scratch_alignment = 256;
	};

	VkFormat to_vk_format(Format format);

	void vulkan_resources_init(ResourceManager& manager, VmaAllocator allocator, const DeviceContext& device);
	void vulkan_resources_destroy(ResourceManager& manager);

	AccelStructHandle vulkan_create_acceleration_structure(ResourceManager& manager, const AccelStructDescription& description);
	uint64_t vulkan_get_accel_address(ResourceManager& manager, AccelStructHandle handle);
	void vulkan_destroy_acceleration_structure(ResourceManager& manager, AccelStructHandle handle, FrameContext& frame);

	void vulkan_immediate_submit(ResourceManager& manager, std::function<void(VkCommandBuffer)>&& record);

	void vulkan_resources_push_deletor(DeletionQueue& deletion_queue, std::function<void()>&& fn);
	void vulkan_resources_flush_deletors(DeletionQueue& deletion_queue);

	BufferHandle vulkan_create_buffer(ResourceManager& manager, const BufferDescription& buffer_description);
	VkBuffer vulkan_get_buffer(const ResourceManager& manager, BufferHandle handle);
	uint64_t vulkan_get_buffer_address(const ResourceManager& manager, BufferHandle handle);
	void vulkan_upload_buffer(ResourceManager& manager, BufferHandle dst, const void* data, size_t size);
	void vulkan_destroy_buffer(ResourceManager&, BufferHandle handle, FrameContext& frame);

	ImageHandle vulkan_create_image(ResourceManager& manager, const ImageDescription& description);
	AllocatedImage& vulkan_get_image(ResourceManager& manager, ImageHandle handle);
	void vulkan_upload_image(ResourceManager& manager, ImageHandle handle, const void* pixels, size_t size);
	void vulkan_destroy_image(ResourceManager& manager, ImageHandle handle, FrameContext& frame);

	Sampler vulkan_create_sampler(ResourceManager& manager, const SamplerDescription& description);
	void vulkan_destroy_sampler(ResourceManager& manager, Sampler sampler, FrameContext& frame);
}
