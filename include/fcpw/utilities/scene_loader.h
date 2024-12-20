#pragma once

#include <fcpw/fcpw.h>

namespace fcpw {

enum class LoadingOption {
    ObjLineSegments,
    ObjTriangles
};

extern std::vector<std::pair<std::string, LoadingOption>> files;
extern std::string instanceFilename;
extern std::string csgFilename;

template<size_t DIM>
class SceneLoader {
public:
    // loads files; NOTE: this method does not build the scene aggregate/accelerator,
    // it just populates its geometry (but not the silhouettes)
    void loadFiles(Scene<DIM>& scene, bool computeNormals);
};

} // namespace fcpw

#include "scene_loader.inl"