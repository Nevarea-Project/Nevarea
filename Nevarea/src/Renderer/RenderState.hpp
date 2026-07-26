#pragma once

#include "Vulkan/VulkanContext.hpp"
#include "lib/Rendering.hpp"

namespace Nevarea {
	struct RenderState {
		RenderingAPI api = RenderingAPI::NONE;

        bool is_active = false;

        union {
            Renderer::VulkanContext vulkan;
            // then i could have other contexts here ig
        };

        RenderState() {}
        ~RenderState() {}
    };

    RenderState* resolve(RenderContext context);
}
