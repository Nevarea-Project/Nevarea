#pragma once

#include "WindowSystem.hpp"
#include "Core.hpp"

namespace Nevarea {
	enum class RenderingAPI : uint32_t { NONE, VULKAN };

	enum class Format : uint32_t {
        // 8-bit unorm / srgb
        R8_UNORM, RG8_UNORM, RGBA8_UNORM, RGBA8_SRGB, BGRA8_UNORM, BGRA8_SRGB,
        // float
        R16_SFLOAT, RG16_SFLOAT, RGBA16_SFLOAT,
        R32_SFLOAT, RG32_SFLOAT, RGB32_SFLOAT, RGBA32_SFLOAT,
        // packed
        RGB10A2_UNORM, RG11B10_UFLOAT,
        // integer
        R32_UINT,
        // depth / depth-stencil
        D16_UNORM, D32_SFLOAT, D24_UNORM_S8_UINT, D32_SFLOAT_S8_UINT,
        // bc
        BC1_RGBA_UNORM, BC1_RGBA_SRGB,
        BC3_UNORM, BC3_SRGB,
        BC4_UNORM,
        BC5_UNORM,
        BC7_UNORM, BC7_SRGB,

        COUNT
    };

   	enum class MemoryLocation : uint32_t { GPU_ONLY, CPU_TO_GPU, GPU_TO_CPU };
	enum class PresentMode : uint32_t {	VSYNC, MAILBOX, IMMEDIATE };
	enum class Filter : uint32_t { NEAREST, LINEAR };
	enum class MipmapMode : uint32_t { NEAREST, LINEAR };
	enum class AddressMode : uint32_t { REPEAT, MIRRORED_REPEAT, CLAMP_EDGE, CLAMP_BORDER };
	enum class CompareOp : uint32_t { NONE, LESS, LESS_EQUAL, GREATER, GREATER_EQUAL, EQUAL, ALWAYS };
	enum class BorderColor : uint32_t { TRANSPARENT_BLACK, OPAQUE_BLACK, OPAQUE_WHITE };
	enum class PrimitiveTopology : uint32_t { TRIANGLE_LIST, TRIANGLE_STRIP, LINE_LIST, POINT_LIST };
    enum class PolygonMode : uint32_t { FILL, LINE, POINT };
    enum class CullMode : uint32_t { NONE, FRONT, BACK };
    enum class FrontFace : uint32_t { CLOCKWISE, COUNTER_CLOCKWISE };

    enum class ImageType : uint32_t {
        D1, D2, D3,
        D1_ARRAY, D2_ARRAY,
        CUBE, CUBE_ARRAY,
    };

    enum class LoadOp : uint32_t { LOAD, CLEAR, DONT_CARE };
    enum class StoreOp : uint32_t { STORE, DONT_CARE };
    enum class AccelType : uint32_t { BOTTOM_LEVEL, TOP_LEVEL, GENERIC };
    enum class AccelBuildMode : uint32_t { BUILD, UPDATE };
    enum class AccelGeometryType : uint32_t { TRIANGLES, AABBS, INSTANCES };
    enum class IndexType : uint32_t { NONE, UINT16, UINT32 };
    enum class ColorSpace : uint32_t { SDR_SRGB, HDR10_ST2084, SCRGB_LINEAR };

	namespace ImageUsage {
		enum : uint32_t {
			STORAGE = 1u << 0,
			SAMPLED = 1u << 1,
			TRANSFER_SRC = 1u << 2,
			TRANSFER_DST = 1u << 3,
			COLOR_TARGET = 1u << 4,
			DEPTH_STENCIL_TARGET = 1u << 5,
		};
	}

	namespace BufferUsage {
        enum : uint32_t {
            STORAGE = 1u << 0, UNIFORM = 1u << 1, INDEX = 1u << 2,
            INDIRECT = 1u << 3, TRANSFER_SRC = 1u << 4, TRANSFER_DST = 1u << 5,
            ACCEL_STORAGE = 1u << 6, ACCEL_INPUT = 1u << 7, SHADER_BINDING_TABLE = 1u << 8
        };
    }

    namespace ImageFlags {
        enum : uint32_t {
            MUTABLE_FORMAT = 1u << 0,
            BLOCK_TEXEL_VIEW = 1u << 1,
            ARRAY_2D_COMPATIBLE = 1u << 2,
        };
    }

    namespace AccelBuildFlags {
        enum : uint32_t {
            ALLOW_UPDATE = 1u << 0, ALLOW_COMPACTION = 1u << 1,
            PREFER_FAST_TRACE = 1u << 2, PREFER_FAST_BUILD = 1u << 3, LOW_MEMORY = 1u << 4,
        };
    }

    namespace AccelGeometryFlags {
        enum : uint32_t { OPAQUE_GEOMETRY = 1u << 0, NO_DUPLICATE_ANY_HIT = 1u << 1 };
    }


	enum class RenderContext : uint32_t { INVALID = 0 };

	struct Image {
		uint32_t id = UINT32_MAX;
		uint32_t generation = 0;
		bool is_valid() const { return id != UINT32_MAX; }
	};

	struct Pipeline {
		uint32_t id = UINT32_MAX;
		uint32_t generation = 0;
		bool is_valid() const { return id != UINT32_MAX; }
	};

	struct Buffer {
        uint32_t id = UINT32_MAX;
        uint32_t generation = 0;
        bool is_valid() const { return id != UINT32_MAX; }
    };

   	struct Sampler {
	    uint32_t id = UINT32_MAX;
		uint32_t generation = 0;
		bool is_valid() const { return id != UINT32_MAX; };
	};

	struct AccelerationStructure {
        uint32_t id = UINT32_MAX;
        uint32_t generation = 0;
        bool is_valid() const { return id != UINT32_MAX; }
    };


    struct AccelGeometry {
        AccelGeometryType type = AccelGeometryType::TRIANGLES;
        uint32_t flags = 0;

        uint64_t vertex_address = 0;
        Format vertex_format = Format::RGB32_SFLOAT;
        uint32_t vertex_stride = 0;
        uint32_t max_vertex = 0;
        uint64_t index_address = 0;
        IndexType index_type = IndexType::UINT32;
        uint64_t transform_address = 0;

        uint64_t aabb_address = 0;
        uint32_t aabb_stride = 0;

        uint64_t instances_address = 0;
        bool instances_array_of_pointers = false;

        uint32_t primitive_count = 0;
        uint32_t primitive_offset = 0;
        uint32_t first_vertex = 0;
        uint32_t transform_offset = 0;
    };

    struct AccelStructDescription {
        AccelType type = AccelType::BOTTOM_LEVEL;
        AccelBuildMode mode = AccelBuildMode::BUILD;
        uint32_t flags = 0;
        const AccelGeometry* geometries = nullptr;
        uint32_t geometry_count = 0;
    };

	struct ImageDescription {
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t depth = 1;
		uint32_t usage = 0;
		uint32_t flags = 0;
		uint32_t mip_levels = 0;
		uint32_t array_layers = 1;
		uint32_t samples = 1;

		Format format = Format::RGBA16_SFLOAT;
		MemoryLocation memory_location = MemoryLocation::GPU_ONLY;
		ImageType image_type = ImageType::D2;

		float priority = 0.5f;
	};

	struct BufferDescription {
		size_t size = 0;
		uint32_t usage = 0;
		MemoryLocation memory = MemoryLocation::CPU_TO_GPU;
		const char* debug_name = "nevarea_buffer";
		float priority = 0.5f;
	};

	struct SamplerDescription {
        Filter min_filter = Filter::LINEAR;
        Filter mag_filter = Filter::LINEAR;
        MipmapMode  mipmap_mode = MipmapMode::LINEAR;
        AddressMode address_u = AddressMode::REPEAT;
        AddressMode address_v = AddressMode::REPEAT;
        AddressMode address_w = AddressMode::REPEAT;

        float mip_lod_bias = 0.0f;
        float min_lod = 0.0f;
        float max_lod = 1000.0f; // ~VK_LOD_CLAMP_NONE meaning that large lods wont get clamped
        float max_anisotropy = 1.0f;

        CompareOp compare_op = CompareOp::NONE;
        BorderColor border_color = BorderColor::OPAQUE_BLACK;
	};

    struct ColorAttachment {
        Image image;
        LoadOp load = LoadOp::CLEAR;
        StoreOp store = StoreOp::STORE;
        float clear[4] = { 0.f, 0.f, 0.f, 1.f };
    };

    struct PipelineDescription {
        const char* vertex_shader = nullptr;
        const char* fragment_shader = nullptr;

        Format color_format = Format::COUNT;
        PrimitiveTopology topology = PrimitiveTopology::TRIANGLE_LIST;
        PolygonMode polygon_mode = PolygonMode::FILL;
        CullMode cull_mode = CullMode::NONE;
        FrontFace front_face = FrontFace::COUNTER_CLOCKWISE;

        bool depth_test = false;
        bool depth_write = false;
        CompareOp depth_compare = CompareOp::LESS_EQUAL;
        Format depth_format = Format::COUNT;

        bool blend_enable = false;
    };

    struct DepthAttachment {
        Image image;
        LoadOp load = LoadOp::CLEAR;
        StoreOp store = StoreOp::STORE;
        float clear = 1.0f;
    };

    struct RenderPassDescription {
        const ColorAttachment* color = nullptr;
        uint32_t color_count = 0;
        DepthAttachment depth;
        bool present = false;
    };

    struct SwapchainDescription {
        Format format = Format::BGRA8_SRGB;
        ColorSpace color_space = ColorSpace::SDR_SRGB;
        PresentMode present_mode = PresentMode::VSYNC;
        uint32_t image_count = 0;
        uint32_t image_usage = ImageUsage::COLOR_TARGET;
    };

    struct DrawCommand {
        Pipeline pipeline;
        Buffer index_buffer;
        uint32_t count = 0;
        uint32_t first = 0;
        int32_t vertex_offset = 0;
        uint32_t instance_count = 1;
        const void* push = nullptr;
        size_t push_size = 0;
    };


	NEVAREA_API RenderContext renderer_create(RenderingAPI api);
	NEVAREA_API void renderer_destroy(RenderContext renderer);

	NEVAREA_API SwapchainDescription renderer_hook_window(RenderContext renderer, WindowHandle window, const SwapchainDescription& swapchain_description = {});
	NEVAREA_API void renderer_draw(RenderContext renderer);

	NEVAREA_API void renderer_begin_pass(RenderContext renderer, const RenderPassDescription& description);
	NEVAREA_API void renderer_end_pass(RenderContext renderer);

	NEVAREA_API void renderer_request_device_extensions(RenderContext renderer, const char* const* names, uint32_t count);
	NEVAREA_API bool renderer_extension_supported(RenderContext renderer, const char* name);
	NEVAREA_API bool renderer_extension_enabled(RenderContext renderer, const char* name);

	NEVAREA_API NvWinExtent renderer_get_swapchain_extent(RenderContext renderer);
	NEVAREA_API bool renderer_swapchain_resized(RenderContext renderer);
	NEVAREA_API SwapchainDescription renderer_update_swapchain(RenderContext renderer, const SwapchainDescription& description = {});
	NEVAREA_API bool renderer_swapchain_format_supported(RenderContext renderer, Format format, ColorSpace color_space);
	NEVAREA_API bool renderer_swapchain_present_mode_supported(RenderContext renderer, PresentMode mode);

	NEVAREA_API Pipeline renderer_create_pipeline(RenderContext renderer, const PipelineDescription& description);
	NEVAREA_API Pipeline renderer_create_compute_pipeline(RenderContext renderer, const char* compute);
	NEVAREA_API void renderer_destroy_pipeline(RenderContext renderer, Pipeline pipeline);

	NEVAREA_API AccelerationStructure renderer_create_acceleration_structure(RenderContext renderer, const AccelStructDescription& description);
	NEVAREA_API uint64_t renderer_get_acceleration_structure_address(RenderContext renderer, AccelerationStructure acceleration_structure);
	NEVAREA_API void renderer_destroy_acceleration_structure(RenderContext renderer, AccelerationStructure acceleration_structure);

	NEVAREA_API Image renderer_create_image(RenderContext renderer, const ImageDescription& description);
	NEVAREA_API void renderer_destroy_image(RenderContext renderer, Image handle);

	NEVAREA_API Sampler renderer_create_sampler(RenderContext renderer, const SamplerDescription& description);
	NEVAREA_API void renderer_destroy_sampler(RenderContext renderer, Sampler sampler);

	NEVAREA_API void renderer_submit(RenderContext renderer, const DrawCommand& draw);
	NEVAREA_API void renderer_dispatch_compute(RenderContext renderer, Pipeline pipeline, uint32_t groups_x, uint32_t groups_y, uint32_t groups_z, const void* push, size_t size);
	NEVAREA_API void renderer_upload_image(RenderContext renderer, Image handle, const void* pixels, size_t size);

	NEVAREA_API Buffer renderer_create_buffer(RenderContext context, const BufferDescription& description);
	NEVAREA_API void renderer_destroy_buffer(RenderContext context, Buffer handle);
	NEVAREA_API void renderer_update_buffer(RenderContext context, Buffer handle, const void* data, size_t size);
	NEVAREA_API uint64_t renderer_get_buffer_address(RenderContext context, Buffer handle);

	NEVAREA_API bool renderer_device_lost(RenderContext renderer);

	NEVAREA_API uint64_t renderer_last_present_id(RenderContext renderer);
	NEVAREA_API bool renderer_wait_for_present(RenderContext renderer, uint64_t present_id, uint64_t timeout_ns);
}
