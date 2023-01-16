// Simple basecode showing how to create a window and attatch a vulkansurface
#define GATEWARE_ENABLE_CORE // All libraries need this
#define GATEWARE_ENABLE_SYSTEM // Graphics libs require system level libraries
#define GATEWARE_ENABLE_GRAPHICS // Enables all Graphics Libraries
// Ignore some GRAPHICS libraries we aren't going to use
#define GATEWARE_DISABLE_GDIRECTX11SURFACE // we have another template for this
#define GATEWARE_DISABLE_GDIRECTX12SURFACE // we have another template for this
#define GATEWARE_DISABLE_GRASTERSURFACE // we have another template for this
#define GATEWARE_DISABLE_GOPENGLSURFACE // we have another template for this

#define GATEWARE_ENABLE_MATH // Enable Gateware’s built-in math library

#define GATEWARE_ENABLE_INPUT
// With what we want & what we don't defined we can include the API
#include "../Gateware/Gateware.h"
#include "renderer.h"
// open some namespaces to compact the code a bit
using namespace GW;
using namespace CORE;
using namespace SYSTEM;
using namespace GRAPHICS;

bool ParseModels(const char* fileName) {
	std::string line;
	std::ifstream fis(fileName);

	if (fis.fail()) return false;

	while (!fis.eof()) {
		std::getline(fis, line);
		if (0 != line.compare("MESH")) continue;
		std::cout << line << std::endl;
		for (int i = 0; i < 5; ++i) {
			std::getline(fis, line);
			std::cout << line << std::endl;
		}
		std::cout << std::endl;
	}

	return true;
}

// lets pop a window and use Vulkan to clear to a red screen
int main()
{
	if (!ParseModels("../GameLevel.txt")) return 0;
	else system("pause");
	GWindow win;
	GEventResponder msgs;
	GVulkanSurface vulkan;
	if (+win.Create(0, 0, 800, 600, GWindowStyle::WINDOWEDBORDERED))
	{
		win.SetWindowName("Daniel Ben Zvi - 3DCC Project");

		VkClearValue clrAndDepth[2];
		clrAndDepth[0].color = { {0.27f, 0.27f, 0.27f, 1} };
		clrAndDepth[1].depthStencil = { 1.0f, 0u };
		msgs.Create([&](const GW::GEvent& e) {
			GW::SYSTEM::GWindow::Events q;
			if (+e.Read(q))
				switch (q) {
					case GWindow::Events::RESIZE:
						//clrAndDepth[0].color.float32[2] += 0.01f; // disable
						break;
				}
			});
		win.Register(msgs);
#ifndef NDEBUG
		const char* debugLayers[] = {
			"VK_LAYER_KHRONOS_validation", // standard validation layer
			//"VK_LAYER_LUNARG_standard_validation", // add if not on MacOS		//	is Deprecated
			"VK_LAYER_RENDERDOC_Capture" // add this if you have installed RenderDoc
		};
		/*for (const char* name : debugLayers) {
			bool found = false;
			uint32_t layerCount;
			vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

			std::vector<VkLayerProperties> availableLayers(layerCount);
			vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

			for (const auto& layerProperties : availableLayers) {
				if (strcmp(name, layerProperties.layerName) == 0) {
					found = true;
					break;
				}
			}
		}*/	//	delete
		if (+vulkan.Create(	win, GW::GRAPHICS::DEPTH_BUFFER_SUPPORT, 
							sizeof(debugLayers)/sizeof(debugLayers[0]),
							debugLayers, 0, nullptr, 0, nullptr, false))
#else
		if (+vulkan.Create(win, GW::GRAPHICS::DEPTH_BUFFER_SUPPORT))
#endif
		{
			Renderer renderer(win, vulkan);
			while (+win.ProcessWindowEvents())
			{
				if (+vulkan.StartFrame(2, clrAndDepth))
				{
					renderer.UpdateCamera();
					renderer.Render();
					vulkan.EndFrame(true);
				}
			}
		}
	}
	return 0; // that's all folks
}
