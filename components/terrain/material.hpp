#ifndef COMPONENTS_TERRAIN_MATERIAL_H
#define COMPONENTS_TERRAIN_MATERIAL_H

#include <osg/StateSet>

namespace osg
{
    class Texture2D;
}

namespace Resource
{
    class SceneManager;
}

namespace MWRender
{
    class SnowDeformationManager;
}

namespace Terrain
{

    struct TextureLayer
    {
        osg::ref_ptr<osg::Texture2D> mDiffuseMap;
        osg::ref_ptr<osg::Texture2D> mNormalMap; // optional
        bool mParallax = false;
        bool mSpecular = false;
    };

    std::vector<osg::ref_ptr<osg::StateSet>> createPasses(bool useShaders, Resource::SceneManager* sceneManager,
        const std::vector<TextureLayer>& layers, const std::vector<osg::ref_ptr<osg::Texture2D>>& blendmaps,
        int blendmapScale, float layerTileSize, bool esm4terrain = false);

    /// Set snow deformation data for terrain integration (called by RenderingManager)
    void setSnowDeformationData(bool enabled, osg::Texture2D* texture, float strength,
                                 const osg::Vec2f& textureCenter, float worldTextureSize);
}

#endif
