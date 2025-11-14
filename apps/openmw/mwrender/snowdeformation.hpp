#ifndef OPENMW_MWRENDER_SNOWDEFORMATION_HPP
#define OPENMW_MWRENDER_SNOWDEFORMATION_HPP

#include <osg/Group>
#include <osg/Texture2D>
#include <osg/Camera>
#include <osg/Vec3f>
#include <osg/Vec2f>
#include <memory>
#include <vector>

namespace osg
{
    class Geometry;
    class StateSet;
}

namespace Resource
{
    class ResourceSystem;
}

namespace Shader
{
    class ShaderManager;
}

namespace MWRender
{
    class Camera;

    /// Manages real-time terrain deformation for snow surfaces
    /// Uses dense mesh overlay + vertex texture fetch (VTF) approach
    class SnowDeformationManager
    {
    public:
        SnowDeformationManager(osg::ref_ptr<osg::Group> rootNode, Resource::ResourceSystem* resourceSystem);
        ~SnowDeformationManager();

        /// Update deformation based on player movement
        /// @param playerPos Current player position in world space
        /// @param dt Delta time in seconds
        void update(const osg::Vec3f& playerPos, float dt);

        /// Enable or disable the snow deformation system
        void setEnabled(bool enabled);
        bool isEnabled() const { return mEnabled; }

        /// Set the radius around player where deformation is active (default: 2.0 units)
        void setDeformationRadius(float radius);
        float getDeformationRadius() const { return mDeformationRadius; }

        /// Set deformation strength multiplier (default: 1.0)
        void setDeformationStrength(float strength);
        float getDeformationStrength() const { return mDeformationStrength; }

        /// Get the deformation texture (for debugging or shader access)
        osg::Texture2D* getDeformationTexture() const { return mDeformationTexture.get(); }

        /// Footprint data structure (public for DeformationTextureRenderer access)
        struct Footprint
        {
            osg::Vec2f position;       // XY position in world space
            float intensity;           // 0.0 to 1.0, decays over time
            float radius;              // Footprint radius
            float timestamp;           // For decay calculation
        };

    private:
        void initialize();
        void createDeformationTexture();
        void createDeformationCamera();
        void createDeformationMesh();
        void updateDeformationTexture(const osg::Vec3f& playerPos, float dt);
        void updateMeshPosition(const osg::Vec3f& playerPos);
        void renderFootprintsToTexture();
        void applyDecayPass();
        osg::Geometry* createFootprintQuad(const Footprint& footprint);
        osg::Geometry* createFullscreenQuad();
        int detectTerrainMaterial(const osg::Vec3f& worldPos);

        osg::ref_ptr<osg::Group> mRootNode;
        Resource::ResourceSystem* mResourceSystem;

        // Deformation texture (RTT - Render To Texture with ping-pong)
        osg::ref_ptr<osg::Texture2D> mDeformationTexture;
        osg::ref_ptr<osg::Texture2D> mDeformationTextureBack;
        osg::ref_ptr<osg::Camera> mDeformationCamera;
        osg::ref_ptr<osg::Camera> mDecayCamera;
        osg::ref_ptr<osg::Geometry> mDecayQuad;

        // Dense overlay mesh that follows player
        osg::ref_ptr<osg::Group> mDeformationMeshGroup;
        osg::ref_ptr<osg::Geometry> mDenseMesh;
        osg::ref_ptr<osg::StateSet> mDeformationStateSet;

        // State
        bool mEnabled;
        int mTerrainMaterialType;      // Current terrain material (1=snow, 2=sand, 3=ash)
        float mDeformationRadius;      // Radius around player for deformation
        float mDeformationStrength;    // Multiplier for deformation depth
        osg::Vec3f mLastPlayerPos;     // For detecting movement
        float mWorldTextureSize;       // World units covered by 1024px texture
        osg::Vec2f mTextureCenter;     // Current center of deformation texture in world space

        // Footprint tracking
        std::vector<Footprint> mFootprints;
        float mFootprintInterval;      // Minimum distance between footprints
        float mLastFootprintDist;      // Distance traveled since last footprint
        float mDecayRate;              // How fast footprints fade (per second)

        static constexpr int DEFORMATION_TEXTURE_SIZE = 1024;
        static constexpr float DEFAULT_DEFORMATION_RADIUS = 50.0f;   // 50 units ≈ 10-15 meters
        static constexpr float DEFAULT_WORLD_TEXTURE_SIZE = 200.0f;  // 200x200 world units to match larger radius
        static constexpr float DEFAULT_FOOTPRINT_INTERVAL = 5.0f;    // New footprint every 5 units (1 meter)
        static constexpr float DEFAULT_DECAY_RATE = 0.1f;            // 10% fade per second
    };

    /// Generates dense terrain mesh geometry for smooth deformation
    class DeformationMeshGenerator
    {
    public:
        /// Generate a dense grid mesh
        /// @param size World size of the mesh (both X and Z)
        /// @param resolution Number of vertices per side (e.g., 128 = 128x128 grid)
        /// @param center Center position in world space
        /// @return Dense mesh geometry ready for VTF displacement
        static osg::ref_ptr<osg::Geometry> createDenseMesh(
            float size,
            int resolution,
            const osg::Vec2f& center);

        /// Update mesh UVs based on deformation texture scrolling
        /// @param geometry The mesh to update
        /// @param worldCenter Current mesh center in world space
        /// @param textureCenter Current texture center in world space
        /// @param textureWorldSize Size of world area covered by texture
        static void updateMeshUVs(
            osg::Geometry* geometry,
            const osg::Vec2f& worldCenter,
            const osg::Vec2f& textureCenter,
            float textureWorldSize);
    };

    /// Handles render-to-texture for deformation heightmap
    class DeformationTextureRenderer
    {
    public:
        DeformationTextureRenderer(Resource::ResourceSystem* resourceSystem);

        /// Create the RTT camera and texture
        void initialize(int textureSize);

        /// Render footprints to deformation texture
        /// @param footprints Active footprints to render
        /// @param textureCenter Center of texture in world space
        /// @param worldSize Size of world area covered by texture
        void renderFootprints(
            const std::vector<struct SnowDeformationManager::Footprint>& footprints,
            const osg::Vec2f& textureCenter,
            float worldSize);

        /// Apply decay/settling to existing deformation
        void applyDecay(float decayFactor);

        osg::Texture2D* getTexture() const { return mTexture.get(); }
        osg::Camera* getCamera() const { return mCamera.get(); }

    private:
        void createQuadGeometry();

        Resource::ResourceSystem* mResourceSystem;
        osg::ref_ptr<osg::Texture2D> mTexture;
        osg::ref_ptr<osg::Camera> mCamera;
        osg::ref_ptr<osg::Geometry> mQuad;
        osg::ref_ptr<osg::StateSet> mDecayStateSet;
        osg::ref_ptr<osg::StateSet> mFootprintStateSet;
    };

} // namespace MWRender

#endif // OPENMW_MWRENDER_SNOWDEFORMATION_HPP
