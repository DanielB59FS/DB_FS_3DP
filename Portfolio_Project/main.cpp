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
#include <regex>
// open some namespaces to compact the code a bit
using namespace GW;
using namespace CORE;
using namespace SYSTEM;
using namespace GRAPHICS;

// lets pop a window and use Vulkan to clear to a red screen
int main()
{
	/*std::string derp;
	std::string* derp2 = &derp;
	scanf("%s", &derp);
	printf("%s\n", derp);
	printf("%p\n%p\n%p\n", &derp, derp.c_str(), derp2);
	return 0;*/
	//return 0;
	//std::string subject1 = "<Matrix 4x4 (1.0000, 0.0000,   0.0000, 0.0000)\n",
	//	subject2 = "            (0.0000, 1.0000,  -0.0000, 0.0000)\n", pattern = "\1bcd";
	//float x = -1, y = -1, z = -1, w = -1;
	//std::regex rgx (pattern, std::regex::ECMAScript | std::regex::icase);
	//std::cmatch match;
	////std::regex_match("<Matrix 4x4 (1.0000, 0.0000,   0.0000, 0.0000)\n", match, rgx);
	////std::regex_search("12 243 46 76 32", match, rgx);
	//std::regex_match("abcd", match, rgx);
	//std::cout << "matches:\n";
	//for (int i = 0; i < match.size(); ++i)
	//	std::cout << match[i] << std::endl;
	//return 0;
	//H2B::Parser h2bParser;
	//h2bParser.Parse("../Assets/bomb.h2b");
	//return 0;
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

		if (+vulkan.Create(	win, GW::GRAPHICS::DEPTH_BUFFER_SUPPORT, 
							sizeof(debugLayers)/sizeof(debugLayers[0]),
							debugLayers, 0, nullptr, 0, nullptr, false))
#else
		if (+vulkan.Create(win, GW::GRAPHICS::DEPTH_BUFFER_SUPPORT))
#endif
		{
			Renderer renderer(win, vulkan);
			//if (!renderer.ParseLevel("../GameLevel.txt")) return 0;
			while (+win.ProcessWindowEvents())
			{
				if (+vulkan.StartFrame(2, clrAndDepth))
				{
					renderer.Update();
					renderer.Render();
					vulkan.EndFrame(true);
				}
			}
		}
	}
	return 0; // that's all folks
}
