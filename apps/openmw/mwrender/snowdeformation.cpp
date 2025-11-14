#include "snowdeformation.hpp"

#include <osg/Geometry>
#include <osg/Geode>
#include <osg/Texture2D>
#include <osg/Camera>
#include <osg/FrameBufferObject>
#include <osg/Material>
#include <osg/BlendFunc>
#include <osg/Depth>
#include <osg/MatrixTransform>
#include <osgUtil/CullVisitor>

#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/shader/shadermanager.hpp>
#include <components/settings/values.hpp>
#include <components/debug/debuglog.hpp>

#include "vismask.hpp"

namespace MWRender
{

// ============================================================================
// SnowDeformationManager Implementation
// ============================================================================

SnowDeformationManager::SnowDeformationManager(osg::ref_ptr<osg::Group> rootNode, Resource::ResourceSystem* resourceSystem)
    : mRootNode(rootNode)
    , mResourceSystem(resourceSystem)
    , mEnabled(true)
    , mTerrainMaterialType(1)  // Default to snow
    , mDeformationRadius(DEFAULT_DEFORMATION_RADIUS)
    , mDeformationStrength(3.0f)  // Multiplier for displacement (higher = deeper snow trails)
    , mLastPlayerPos(0, 0, 0)
    , mWorldTextureSize(DEFAULT_WORLD_TEXTURE_SIZE)
    , mTextureCenter(0, 0)
    , mFootprintInterval(DEFAULT_FOOTPRINT_INTERVAL)
    , mLastFootprintDist(0.0f)
    , mDecayRate(DEFAULT_DECAY_RATE)
{
    initialize();
}

SnowDeformationManager::~SnowDeformationManager()
{
    if (mDeformationMeshGroup && mRootNode)
    {
        mRootNode->removeChild(mDeformationMeshGroup);
    }
}

void SnowDeformationManager::initialize()
{
    // Create deformation texture (RTT)
    createDeformationTexture();

    // Create the camera that renders to the deformation texture
    createDeformationCamera();

    // Create the dense mesh that will be displaced
    createDeformationMesh();
}

void SnowDeformationManager::createDeformationTexture()
{
    // Create front buffer
    mDeformationTexture = new osg::Texture2D;
    mDeformationTexture->setTextureSize(DEFORMATION_TEXTURE_SIZE, DEFORMATION_TEXTURE_SIZE);
    mDeformationTexture->setInternalFormat(GL_R16F); // 16-bit float for height values
    mDeformationTexture->setSourceFormat(GL_RED);
    mDeformationTexture->setSourceType(GL_FLOAT);
    mDeformationTexture->setFilter(osg::Texture2D::MIN_FILTER, osg::Texture2D::LINEAR);
    mDeformationTexture->setFilter(osg::Texture2D::MAG_FILTER, osg::Texture2D::LINEAR);
    mDeformationTexture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
    mDeformationTexture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);

    // Create back buffer for ping-pong
    mDeformationTextureBack = new osg::Texture2D;
    mDeformationTextureBack->setTextureSize(DEFORMATION_TEXTURE_SIZE, DEFORMATION_TEXTURE_SIZE);
    mDeformationTextureBack->setInternalFormat(GL_R16F);
    mDeformationTextureBack->setSourceFormat(GL_RED);
    mDeformationTextureBack->setSourceType(GL_FLOAT);
    mDeformationTextureBack->setFilter(osg::Texture2D::MIN_FILTER, osg::Texture2D::LINEAR);
    mDeformationTextureBack->setFilter(osg::Texture2D::MAG_FILTER, osg::Texture2D::LINEAR);
    mDeformationTextureBack->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
    mDeformationTextureBack->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
}

void SnowDeformationManager::createDeformationCamera()
{
    // Camera for decay pass - renders FIRST (order 0)
    mDecayCamera = new osg::Camera;
    mDecayCamera->setRenderOrder(osg::Camera::PRE_RENDER, 0);  // Order 0 - renders first
    mDecayCamera->setRenderTargetImplementation(osg::Camera::FRAME_BUFFER_OBJECT);
    mDecayCamera->setReferenceFrame(osg::Camera::ABSOLUTE_RF);
    mDecayCamera->setProjectionMatrixAsOrtho2D(0, 1, 0, 1);
    mDecayCamera->setViewMatrix(osg::Matrix::identity());
    mDecayCamera->setClearColor(osg::Vec4(0, 0, 0, 0));
    mDecayCamera->setClearMask(GL_COLOR_BUFFER_BIT);  // Clear to black before decay pass
    mDecayCamera->attach(osg::Camera::COLOR_BUFFER0, mDeformationTextureBack.get());
    mDecayCamera->setViewport(0, 0, DEFORMATION_TEXTURE_SIZE, DEFORMATION_TEXTURE_SIZE);

    // Set proper node mask for RTT
    mDecayCamera->setNodeMask(Mask_RenderToTexture);
    mDecayCamera->setCullingActive(false);

    // Create fullscreen quad for decay
    mDecayQuad = createFullscreenQuad();

    // Set up decay shader (once, not every frame)
    Shader::ShaderManager& shaderMgr = mResourceSystem->getSceneManager()->getShaderManager();
    Shader::ShaderManager::DefineMap defines;
    osg::ref_ptr<osg::Program> decayProgram = shaderMgr.getProgram("compatibility/snow_decay", defines);

    osg::ref_ptr<osg::StateSet> decayState = mDecayQuad->getOrCreateStateSet();
    decayState->setAttributeAndModes(decayProgram, osg::StateAttribute::ON);
    decayState->setTextureAttributeAndModes(0, mDeformationTexture.get(), osg::StateAttribute::ON);
    decayState->addUniform(new osg::Uniform("deformationMap", 0));
    decayState->addUniform(new osg::Uniform("decayFactor", 0.99f));  // Will be updated each frame

    mDecayCamera->addChild(mDecayQuad);

    mRootNode->addChild(mDecayCamera);

    // Camera for rendering footprints - renders SECOND (order 1)
    mDeformationCamera = new osg::Camera;
    mDeformationCamera->setRenderOrder(osg::Camera::PRE_RENDER, 1);  // Order 1 - renders after decay
    mDeformationCamera->setRenderTargetImplementation(osg::Camera::FRAME_BUFFER_OBJECT);
    mDeformationCamera->setReferenceFrame(osg::Camera::ABSOLUTE_RF);
    mDeformationCamera->setProjectionMatrixAsOrtho2D(0, 1, 0, 1);
    mDeformationCamera->setViewMatrix(osg::Matrix::identity());
    mDeformationCamera->setClearMask(0); // Don't clear, we add to the decayed texture
    mDeformationCamera->attach(osg::Camera::COLOR_BUFFER0, mDeformationTextureBack.get());
    mDeformationCamera->setViewport(0, 0, DEFORMATION_TEXTURE_SIZE, DEFORMATION_TEXTURE_SIZE);

    // Set proper node mask for RTT
    mDeformationCamera->setNodeMask(Mask_RenderToTexture);
    mDeformationCamera->setCullingActive(false);

    // Enable additive blending for footprint accumulation
    osg::ref_ptr<osg::BlendFunc> blendFunc = new osg::BlendFunc(osg::BlendFunc::ONE, osg::BlendFunc::ONE);
    mDeformationCamera->getOrCreateStateSet()->setAttributeAndModes(blendFunc, osg::StateAttribute::ON);

    mRootNode->addChild(mDeformationCamera);

    Log(Debug::Info) << "SnowDeformation: RTT cameras created (decay order=0, footprints order=1)";
}

void SnowDeformationManager::createDeformationMesh()
{
    // Create dense mesh with high vertex density
    const int resolution = 128; // 128x128 = 16,384 vertices
    const float meshSize = mDeformationRadius * 2.0f;

    mDenseMesh = DeformationMeshGenerator::createDenseMesh(
        meshSize,
        resolution,
        osg::Vec2f(0, 0)
    );

    // Create state set with deformation shader
    mDeformationStateSet = new osg::StateSet;

    // Get shader manager
    Shader::ShaderManager& shaderMgr = mResourceSystem->getSceneManager()->getShaderManager();

    // Set up shader defines
    Shader::ShaderManager::DefineMap defines;
    defines["@snowDeformation"] = "1";
    defines["@normalMap"] = "1";

    // Get or create deformation shader program
    osg::ref_ptr<osg::Program> program = shaderMgr.getProgram("compatibility/snow_deformation", defines);
        mDeformationStateSet->setAttributeAndModes(program, osg::StateAttribute::ON);


    // Bind deformation texture to texture unit 7 (avoid conflicts with terrain textures 0-6)
    mDeformationStateSet->setTextureAttributeAndModes(7, mDeformationTexture.get(), osg::StateAttribute::ON);
    mDeformationStateSet->addUniform(new osg::Uniform("deformationMap", 7));

    // Set uniforms for deformation parameters
    mDeformationStateSet->addUniform(new osg::Uniform("deformationStrength", mDeformationStrength));
    mDeformationStateSet->addUniform(new osg::Uniform("worldTextureSize", mWorldTextureSize));
    mDeformationStateSet->addUniform(new osg::Uniform("textureCenter", mTextureCenter));

    // Material setup
    osg::ref_ptr<osg::Material> material = new osg::Material;
    material->setDiffuse(osg::Material::FRONT_AND_BACK, osg::Vec4(1.0f, 1.0f, 1.0f, 1.0f));
    material->setAmbient(osg::Material::FRONT_AND_BACK, osg::Vec4(0.8f, 0.8f, 0.8f, 1.0f));
    material->setSpecular(osg::Material::FRONT_AND_BACK, osg::Vec4(0.2f, 0.2f, 0.2f, 1.0f));
    material->setShininess(osg::Material::FRONT_AND_BACK, 8.0f);
    mDeformationStateSet->setAttributeAndModes(material, osg::StateAttribute::ON);

    // Enable blending for smooth overlay
    osg::ref_ptr<osg::BlendFunc> blendFunc = new osg::BlendFunc(
        osg::BlendFunc::SRC_ALPHA,
        osg::BlendFunc::ONE_MINUS_SRC_ALPHA
    );
    mDeformationStateSet->setAttributeAndModes(blendFunc, osg::StateAttribute::ON);

    // Apply state set to mesh
    mDenseMesh->setStateSet(mDeformationStateSet.get());

    // Create MatrixTransform and add mesh
    osg::ref_ptr<osg::MatrixTransform> meshTransform = new osg::MatrixTransform;
    meshTransform->addChild(mDenseMesh.get());
    meshTransform->setNodeMask(Mask_Terrain); // Same visibility as terrain

    mDeformationMeshGroup = meshTransform;

    // Add to scene
    mRootNode->addChild(mDeformationMeshGroup);

    Log(Debug::Info) << "SnowDeformation: Dense mesh created with " << resolution << "x" << resolution
                     << " vertices, size=" << meshSize << " units, mask=" << meshTransform->getNodeMask();
}

void SnowDeformationManager::update(const osg::Vec3f& playerPos, float dt)
{
    if (!mEnabled)
        return;

    static int logCounter = 0;
    if (++logCounter % 60 == 0) // Log every 60 frames (~1 second)
    {
        Log(Debug::Info) << "SnowDeformation: Active footprints=" << mFootprints.size()
                         << " PlayerPos=(" << playerPos.x() << "," << playerPos.y() << "," << playerPos.z() << ")";
    }

 

    // Calculate distance moved
    float distMoved = (playerPos - mLastPlayerPos).length();
    mLastFootprintDist += distMoved;

    // Add new footprint if player has moved enough
    if (mLastFootprintDist >= mFootprintInterval)
    {
        Footprint footprint;
        footprint.position = osg::Vec2f(playerPos.x(), playerPos.y());
        footprint.intensity = 0.3f;  // Intensity in texture (0-1 range, will be multiplied by deformationStrength)
        footprint.radius = 3.0f;     // 3 units ≈ 60cm radius per footprint
        footprint.timestamp = 0.0f;

        mFootprints.push_back(footprint);
        mLastFootprintDist = 0.0f;

        Log(Debug::Info) << "SnowDeformation: Added footprint at (" << footprint.position.x() << "," << footprint.position.y() << ") intensity=" << footprint.intensity;
    }

    // Update existing footprints (decay)
    for (auto it = mFootprints.begin(); it != mFootprints.end();)
    {
        it->timestamp += dt;
        it->intensity = 1.0f - (it->timestamp * mDecayRate);

        // Remove fully decayed footprints
        if (it->intensity <= 0.0f)
        {
            it = mFootprints.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // Update texture center to follow player (with some smoothing)
    osg::Vec2f targetCenter(playerPos.x(), playerPos.y());
    mTextureCenter = mTextureCenter * 0.9f + targetCenter * 0.1f; // Smooth follow

    // Update deformation texture
    updateDeformationTexture(playerPos, dt);

    // Update mesh position to follow player
    updateMeshPosition(playerPos);

    // Update shader uniforms
    mDeformationStateSet->getUniform("textureCenter")->set(mTextureCenter);
    mDeformationStateSet->getUniform("deformationStrength")->set(mDeformationStrength);

    mLastPlayerPos = playerPos;
}

void SnowDeformationManager::updateDeformationTexture(const osg::Vec3f& playerPos, float dt)
{
    static int updateCounter = 0;
    bool shouldLog = (++updateCounter % 60 == 0);  // Log every ~1 second

    if (shouldLog)
    {
        Log(Debug::Info) << "SnowDeformation: updateDeformationTexture - " << mFootprints.size()
                         << " footprints, texture center=(" << mTextureCenter.x() << "," << mTextureCenter.y() << ")";
    }

    // Step 1: Apply decay to existing deformation (always, even with no footprints)
    applyDecayPass();

    // Step 2: Render new footprints (if any)
    if (!mFootprints.empty())
    {
        renderFootprintsToTexture();
        if (shouldLog)
        {
            Log(Debug::Info) << "SnowDeformation: Rendered " << mFootprints.size() << " footprints to texture";
        }
    }

    // Step 3: Swap textures (ping-pong)
    std::swap(mDeformationTexture, mDeformationTextureBack);

    // Update the mesh shader to use the new front buffer
    mDeformationStateSet->setTextureAttributeAndModes(7, mDeformationTexture.get(), osg::StateAttribute::ON);
}

void SnowDeformationManager::updateMeshPosition(const osg::Vec3f& playerPos)
{
    // Position mesh centered on player (XYZ - follow player height!)
    osg::Matrix transform;
    transform.makeTranslate(playerPos.x(), playerPos.y(), playerPos.z());

    // Update mesh transform (we know it's a MatrixTransform from createDeformationMesh)
    osg::MatrixTransform* meshTransform = static_cast<osg::MatrixTransform*>(mDeformationMeshGroup.get());
    meshTransform->setMatrix(transform);
}

void SnowDeformationManager::setEnabled(bool enabled)
{
    mEnabled = enabled;
    if (mDeformationMeshGroup)
    {
        mDeformationMeshGroup->setNodeMask(enabled ? Mask_Terrain : 0);
    }
}

void SnowDeformationManager::setDeformationRadius(float radius)
{
    mDeformationRadius = radius;
    // Recreate mesh with new size
    createDeformationMesh();
}

void SnowDeformationManager::setDeformationStrength(float strength)
{
    mDeformationStrength = strength;
}

// ============================================================================
// DeformationMeshGenerator Implementation
// ============================================================================

osg::ref_ptr<osg::Geometry> DeformationMeshGenerator::createDenseMesh(
    float size,
    int resolution,
    const osg::Vec2f& center)
{
    osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;

    // Vertex arrays
    osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
    osg::ref_ptr<osg::Vec3Array> normals = new osg::Vec3Array;
    osg::ref_ptr<osg::Vec2Array> texCoords = new osg::Vec2Array;

    // Generate grid vertices
    const float halfSize = size * 0.5f;
    const float step = size / (resolution - 1);

    for (int y = 0; y < resolution; ++y)
    {
        for (int x = 0; x < resolution; ++x)
        {
            // Vertex position (centered on origin, will be transformed to player position)
            float vx = -halfSize + x * step;
            float vy = -halfSize + y * step;
            vertices->push_back(osg::Vec3(vx, vy, 0.0f)); // Z will be displaced by shader

            // Normal (pointing up, will be recalculated in shader)
            normals->push_back(osg::Vec3(0.0f, 0.0f, 1.0f));

            // Texture coordinates (0 to 1 range for deformation texture sampling)
            float u = static_cast<float>(x) / (resolution - 1);
            float v = static_cast<float>(y) / (resolution - 1);
            texCoords->push_back(osg::Vec2(u, v));
        }
    }

    geometry->setVertexArray(vertices);
    geometry->setNormalArray(normals, osg::Array::BIND_PER_VERTEX);
    geometry->setTexCoordArray(0, texCoords);

    // Generate triangle indices
    osg::ref_ptr<osg::DrawElementsUInt> indices = new osg::DrawElementsUInt(GL_TRIANGLES);

    for (int y = 0; y < resolution - 1; ++y)
    {
        for (int x = 0; x < resolution - 1; ++x)
        {
            int i0 = y * resolution + x;
            int i1 = i0 + 1;
            int i2 = i0 + resolution;
            int i3 = i2 + 1;

            // Two triangles per quad
            indices->push_back(i0);
            indices->push_back(i2);
            indices->push_back(i1);

            indices->push_back(i1);
            indices->push_back(i2);
            indices->push_back(i3);
        }
    }

    geometry->addPrimitiveSet(indices);

    // Use VBOs for better performance
    geometry->setUseDisplayList(false);
    geometry->setUseVertexBufferObjects(true);

    return geometry;
}

void DeformationMeshGenerator::updateMeshUVs(
    osg::Geometry* geometry,
    const osg::Vec2f& worldCenter,
    const osg::Vec2f& textureCenter,
    float textureWorldSize)
{
    // TODO: Update UVs based on scrolling texture
    // This will map world positions to texture coordinates accounting for texture scrolling
}

// ============================================================================
// DeformationTextureRenderer Implementation
// ============================================================================

DeformationTextureRenderer::DeformationTextureRenderer(Resource::ResourceSystem* resourceSystem)
    : mResourceSystem(resourceSystem)
{
}

void DeformationTextureRenderer::initialize(int textureSize)
{
    // Create texture
    mTexture = new osg::Texture2D;
    mTexture->setTextureSize(textureSize, textureSize);
    mTexture->setInternalFormat(GL_R16F);
    mTexture->setSourceFormat(GL_RED);
    mTexture->setSourceType(GL_FLOAT);
    mTexture->setFilter(osg::Texture2D::MIN_FILTER, osg::Texture2D::LINEAR);
    mTexture->setFilter(osg::Texture2D::MAG_FILTER, osg::Texture2D::LINEAR);
    mTexture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
    mTexture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);

    // Create RTT camera
    mCamera = new osg::Camera;
    mCamera->setRenderOrder(osg::Camera::PRE_RENDER);
    mCamera->setRenderTargetImplementation(osg::Camera::FRAME_BUFFER_OBJECT);
    mCamera->setReferenceFrame(osg::Camera::ABSOLUTE_RF);
    mCamera->setProjectionMatrixAsOrtho2D(0, 1, 0, 1);
    mCamera->setViewMatrix(osg::Matrix::identity());
    mCamera->setClearColor(osg::Vec4(0, 0, 0, 1));
    mCamera->attach(osg::Camera::COLOR_BUFFER0, mTexture.get());

    createQuadGeometry();
}

void DeformationTextureRenderer::createQuadGeometry()
{
    // Create fullscreen quad for rendering operations
    mQuad = new osg::Geometry;

    osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
    vertices->push_back(osg::Vec3(0, 0, 0));
    vertices->push_back(osg::Vec3(1, 0, 0));
    vertices->push_back(osg::Vec3(1, 1, 0));
    vertices->push_back(osg::Vec3(0, 1, 0));

    osg::ref_ptr<osg::Vec2Array> texCoords = new osg::Vec2Array;
    texCoords->push_back(osg::Vec2(0, 0));
    texCoords->push_back(osg::Vec2(1, 0));
    texCoords->push_back(osg::Vec2(1, 1));
    texCoords->push_back(osg::Vec2(0, 1));

    mQuad->setVertexArray(vertices);
    mQuad->setTexCoordArray(0, texCoords);
    mQuad->addPrimitiveSet(new osg::DrawArrays(GL_QUADS, 0, 4));
}

void DeformationTextureRenderer::renderFootprints(
    const std::vector<SnowDeformationManager::Footprint>& footprints,
    const osg::Vec2f& textureCenter,
    float worldSize)
{
    // Implementation moved to SnowDeformationManager
}

void DeformationTextureRenderer::applyDecay(float decayFactor)
{
    // Implementation moved to SnowDeformationManager
}

// ============================================================================
// SnowDeformationManager Helper Functions
// ============================================================================

void SnowDeformationManager::renderFootprintsToTexture()
{
    // Clear previous footprint geometry
    mDeformationCamera->removeChildren(0, mDeformationCamera->getNumChildren());

    // Get shader manager
    Shader::ShaderManager& shaderMgr = mResourceSystem->getSceneManager()->getShaderManager();

    // Create shader for footprint rendering
    Shader::ShaderManager::DefineMap defines;
    osg::ref_ptr<osg::Program> footprintProgram = shaderMgr.getProgram("compatibility/snow_footprint", defines);

    // Render each footprint as a small quad
    for (const auto& footprint : mFootprints)
    {
        osg::ref_ptr<osg::Geometry> quad = createFootprintQuad(footprint);

        // Apply footprint shader
        osg::ref_ptr<osg::StateSet> ss = quad->getOrCreateStateSet();
        ss->setAttributeAndModes(footprintProgram, osg::StateAttribute::ON);
        ss->addUniform(new osg::Uniform("footprintIntensity", footprint.intensity));
        ss->addUniform(new osg::Uniform("footprintRadius", footprint.radius));

        mDeformationCamera->addChild(quad);
    }
}

void SnowDeformationManager::applyDecayPass()
{
    // Update decay shader uniforms (shader was set up in createDeformationCamera)
    osg::StateSet* ss = mDecayQuad->getStateSet();
    if (!ss)
        return;

    // Update the input texture (in case we swapped)
    ss->setTextureAttributeAndModes(0, mDeformationTexture.get(), osg::StateAttribute::ON);

    // Update decay factor (frame-rate independent would require dt, but this is simpler)
    // This value multiplies the current deformation each frame
    float decayFactor = 0.995f;  // 0.5% decay per frame at 60fps = ~3 second half-life
    osg::Uniform* decayUniform = ss->getUniform("decayFactor");
    if (decayUniform)
        decayUniform->set(decayFactor);
}

osg::Geometry* SnowDeformationManager::createFootprintQuad(const Footprint& footprint)
{
    osg::ref_ptr<osg::Geometry> quad = new osg::Geometry;

    // Convert world position to texture UV coordinates
    osg::Vec2f offset = footprint.position - mTextureCenter;
    osg::Vec2f centerUV = (offset / mWorldTextureSize) + osg::Vec2f(0.5f, 0.5f);

    // Calculate quad size in UV space
    float quadSizeUV = (footprint.radius * 2.0f) / mWorldTextureSize;

    // Create quad vertices in UV space
    osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
    vertices->push_back(osg::Vec3(centerUV.x() - quadSizeUV * 0.5f, centerUV.y() - quadSizeUV * 0.5f, 0.0f));
    vertices->push_back(osg::Vec3(centerUV.x() + quadSizeUV * 0.5f, centerUV.y() - quadSizeUV * 0.5f, 0.0f));
    vertices->push_back(osg::Vec3(centerUV.x() + quadSizeUV * 0.5f, centerUV.y() + quadSizeUV * 0.5f, 0.0f));
    vertices->push_back(osg::Vec3(centerUV.x() - quadSizeUV * 0.5f, centerUV.y() + quadSizeUV * 0.5f, 0.0f));

    // Texture coordinates (for radial gradient)
    osg::ref_ptr<osg::Vec2Array> texCoords = new osg::Vec2Array;
    texCoords->push_back(osg::Vec2(0.0f, 0.0f));
    texCoords->push_back(osg::Vec2(1.0f, 0.0f));
    texCoords->push_back(osg::Vec2(1.0f, 1.0f));
    texCoords->push_back(osg::Vec2(0.0f, 1.0f));

    quad->setVertexArray(vertices);
    quad->setTexCoordArray(0, texCoords);
    quad->addPrimitiveSet(new osg::DrawArrays(GL_QUADS, 0, 4));

    quad->setUseDisplayList(false);
    quad->setUseVertexBufferObjects(true);

    return quad.release();
}

osg::Geometry* SnowDeformationManager::createFullscreenQuad()
{
    osg::ref_ptr<osg::Geometry> quad = new osg::Geometry;

    osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
    vertices->push_back(osg::Vec3(0.0f, 0.0f, 0.0f));
    vertices->push_back(osg::Vec3(1.0f, 0.0f, 0.0f));
    vertices->push_back(osg::Vec3(1.0f, 1.0f, 0.0f));
    vertices->push_back(osg::Vec3(0.0f, 1.0f, 0.0f));

    osg::ref_ptr<osg::Vec2Array> texCoords = new osg::Vec2Array;
    texCoords->push_back(osg::Vec2(0.0f, 0.0f));
    texCoords->push_back(osg::Vec2(1.0f, 0.0f));
    texCoords->push_back(osg::Vec2(1.0f, 1.0f));
    texCoords->push_back(osg::Vec2(0.0f, 1.0f));

    quad->setVertexArray(vertices);
    quad->setTexCoordArray(0, texCoords);
    quad->addPrimitiveSet(new osg::DrawArrays(GL_QUADS, 0, 4));

    quad->setUseDisplayList(false);
    quad->setUseVertexBufferObjects(true);

    return quad.release();
}

int SnowDeformationManager::detectTerrainMaterial(const osg::Vec3f& worldPos)
{
    // TODO: Implement terrain material detection by querying terrain blend maps
    // For now, return snow (1) as default
    // In the future, this should:
    // 1. Query terrain storage at worldPos
    // 2. Check blend map values
    // 3. Determine dominant material (snow, sand, ash, etc.)
    return 1; // TERRAIN_SNOW
}

} // namespace MWRender
