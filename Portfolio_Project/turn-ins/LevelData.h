// This is a sample of how to load a level in a data oriented fashion.
// Feel free to use this code as a base and tweak it for your needs.

// This reads .h2b files which are optimized binary .obj+.mtl files
#include "h2bParser.h"
#include "../Gateware/Gateware.h"

#include <set>

class LevelData {

	// transfered from parser
	std::set<std::string> level_strings;
public:
	struct LEVEL_MODEL // one model in the level
	{
		unsigned vertexCount, indexCount, materialCount, meshCount;
		unsigned vertexStart, indexStart, materialStart, meshStart, batchStart;
	};
	struct MODEL_INSTANCES // each instance of a model in the level
	{
		unsigned modelIndex, transformStart, transformCount, flags; // flags optional
	};
	struct MATERIAL_TEXTURES // swaps string pointers for loaded texture offsets
	{
		unsigned int albedoIndex, roughnessIndex, metalIndex, normalIndex;
	};
	// All geometry data combined for level to be loaded onto the video card
	std::vector<H2B::VERTEX> levelVertices;
	std::vector<unsigned> levelIndices;
	// All material data used by the level
	std::vector<H2B::MATERIAL> levelMaterials;
	// This could be populated by the Level_Renderer during GPU transfer
	std::vector<MATERIAL_TEXTURES> levelTextures; // same size as LevelMaterials
	// All transform data used by each model
	std::vector<GW::MATH::GMATRIXF> levelTransforms;
	// All required drawing information combined
	std::vector<H2B::BATCH> levelBatches;
	std::vector<H2B::MESH> levelMeshes;
	std::vector<LEVEL_MODEL> levelModels;
	// what we actually draw once loaded (using GPU instancing)
	std::vector<MODEL_INSTANCES> levelInstances;

	// Imports the default level txt format and collects all .h2b data
	bool LoadLevel(const char* gameLevelPath,
				   const char* h2bFolderPath,
				   GW::SYSTEM::GLog log) {
	   // What this does:
	   // Parse GameLevel.txt 
	   // For each model found in the file...
		   // if not encountered create new unique temporary model entry.
			   // Add model transform to a list of transforms for this model.(instances)
		   // if already encountered, just add its transfrom to the existing model entry.
	   // when finished, traverse model entries to import each model's data to the class.
		std::set<MODEL_ENTRY> uniqueModels; // unique models and their locations
		log.LogCategorized("EVENT", "LOADING GAME LEVEL [DATA ORIENTED]");

		UnloadLevel();// clear previous level data if there is any
		if (ReadGameLevel(gameLevelPath, uniqueModels, log) == false) {
			log.LogCategorized("ERROR", "Fatal error reading game level, aborting level load.");
			return false;
		}
		if (ReadAndCombineH2Bs(h2bFolderPath, uniqueModels, log) == false) {
			log.LogCategorized("ERROR", "Fatal error combining H2B mesh data, aborting level load.");
			return false;
		}
		// level loaded into CPU ram
		log.LogCategorized("EVENT", "GAME LEVEL WAS LOADED TO CPU [DATA ORIENTED]");
		return true;
	}
	// used to wipe CPU level data between levels
	void UnloadLevel() {
		level_strings.clear();
		levelVertices.clear();
		levelIndices.clear();
		levelMaterials.clear();
		levelTextures.clear();
		levelBatches.clear();
		levelMeshes.clear();
		levelModels.clear();
		levelTransforms.clear();
		levelInstances.clear();
	}
	// *NO RENDERING/GPU/DRAW LOGIC IN HERE PLEASE* 
	// *DATA ORIENTED SHOULD AIM TO SEPERATE DATA FROM THE LOGIC THAT USES IT*
	// The Level Renderer class is a good place to utilize this data.
	// You can use your chosen API to have one GPU buffer for each type of data.
	// Then you loop through instances using the API features to draw each mesh only once.
private:
	// internal defintion for reading the GameLevel layout 
	struct MODEL_ENTRY {
		std::string modelFile; // path to .h2b file
		mutable std::vector<GW::MATH::GMATRIXF> instances; // where to draw
		bool operator<(const MODEL_ENTRY& cmp) const {
			return modelFile < cmp.modelFile; // you need this for std::set to work
		}
	};
	// internal helper for reading the game level
	bool ReadGameLevel(const char* gameLevelPath,
					   std::set<MODEL_ENTRY> &outModels,
					   GW::SYSTEM::GLog log) {
		log.LogCategorized("MESSAGE", "Begin Reading Game Level Text File.");
		GW::SYSTEM::GFile file;
		file.Create();
		if (-file.OpenTextRead(gameLevelPath)) {
			log.LogCategorized(
				"ERROR", (std::string("Game level not found: ") + gameLevelPath).c_str());
			return false;
		}
		char linebuffer[1024];
		while (+file.ReadLine(linebuffer, 1024, '\n')) {
			// having to have this is a bug, need to have Read/ReadLine return failure at EOF
			if (linebuffer[0] == '\0')
				break;
			if (std::strcmp(linebuffer, "MESH") == 0) {
				file.ReadLine(linebuffer, 1024, '\n');
				log.LogCategorized("INFO", (std::string("Model Detected: ") + linebuffer).c_str());
				// create the model file name from this (strip the .001)
				MODEL_ENTRY add = { linebuffer, };
				add.modelFile = add.modelFile.substr(0, add.modelFile.find_last_of("."));
				add.modelFile += ".h2b";

				// now read the transform data as we will need that regardless
				GW::MATH::GMATRIXF transform;
				for (int i = 0; i < 4; ++i) {
					file.ReadLine(linebuffer, 1024, '\n');
					// read floats
					std::sscanf(linebuffer + 13, "%f, %f, %f, %f",
								&transform.data[0 + i * 4], &transform.data[1 + i * 4],
								&transform.data[2 + i * 4], &transform.data[3 + i * 4]);
				}
				std::string loc = "Location: X ";
				loc += std::to_string(transform.row4.x) + " Y " +
					std::to_string(transform.row4.y) + " Z " + std::to_string(transform.row4.z);
				log.LogCategorized("INFO", loc.c_str());

				// does this model already exist?
				auto found = outModels.find(add);
				if (found == outModels.end()) // no
				{
					add.instances.push_back(transform);
					outModels.insert(add);
				}
				else // yes
					found->instances.push_back(transform);
			}
		}
		log.LogCategorized("MESSAGE", "Game Level File Reading Complete.");
		return true;
	}
	// internal helper for collecting all .h2b data into unified arrays
	bool ReadAndCombineH2Bs(const char* h2bFolderPath,
							const std::set<MODEL_ENTRY>& modelSet,
							GW::SYSTEM::GLog log) {
		log.LogCategorized("MESSAGE", "Begin Importing .H2B File Data.");
		// parse each model adding to overall arrays
		H2B::Parser p; // reads the .h2b format
		const std::string modelPath = h2bFolderPath;
		for (auto i = modelSet.begin(); i != modelSet.end(); ++i) {
			if (p.Parse((modelPath + "/" + i->modelFile).c_str())) {
				log.LogCategorized("INFO", (std::string("H2B Imported: ") + i->modelFile).c_str());
				// transfer all string data
				for (int j = 0; j < p.materialCount; ++j) {
					for (int k = 0; k < 10; ++k) {
						if (*((&p.materials[j].name) + k) != nullptr)
							*((&p.materials[j].name) + k) =
							level_strings.insert(*((&p.materials[j].name) + k)).first->c_str();
					}
				}
				for (int j = 0; j < p.meshCount; ++j) {
					if (p.materials[j].name != nullptr)
						p.materials[j].name =
						level_strings.insert(p.materials[j].name).first->c_str();
				}
				// record sizes
				LEVEL_MODEL model;
				model.vertexCount = p.vertexCount;
				model.indexCount = p.indexCount;
				model.materialCount = p.materialCount;
				model.meshCount = p.meshCount;
				// record offsets
				model.vertexStart = levelVertices.size();
				model.indexStart = levelIndices.size();
				model.materialStart = levelMaterials.size();
				model.batchStart = levelBatches.size();
				model.meshStart = levelMeshes.size();
				// append/move all data
				levelVertices.insert(levelVertices.end(), p.vertices.begin(), p.vertices.end());
				levelIndices.insert(levelIndices.end(), p.indices.begin(), p.indices.end());
				levelMaterials.insert(levelMaterials.end(), p.materials.begin(), p.materials.end());
				levelBatches.insert(levelBatches.end(), p.batches.begin(), p.batches.end());
				levelMeshes.insert(levelMeshes.end(), p.meshes.begin(), p.meshes.end());
				// add level model
				levelModels.push_back(model);
				// add level model instances
				MODEL_INSTANCES instances;
				instances.flags = 0; // shadows? transparency? much we could do with this.
				instances.modelIndex = levelModels.size() - 1;
				instances.transformStart = levelTransforms.size();
				instances.transformCount = i->instances.size();
				levelTransforms.insert(levelTransforms.end(), i->instances.begin(), i->instances.end());
				// add instance set
				levelInstances.push_back(instances);
			}
			else {
				// notify user that a model file is missing but continue loading
				log.LogCategorized("ERROR",
								   (std::string("H2B Not Found: ") + modelPath + "/" + i->modelFile).c_str());
				log.LogCategorized("WARNING", "Loading will continue but model(s) are missing.");
			}
		}
		log.LogCategorized("MESSAGE", "Importing of .H2B File Data Complete.");
		return true;
	}
};

