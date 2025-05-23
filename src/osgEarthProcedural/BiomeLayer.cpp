/* osgEarth
* Copyright 2008-2012 Pelican Mapping
* MIT License
*/
#include "BiomeLayer"
#include <osgEarth/Random>
#include <osgEarth/MetaTile>
#include <random>

using namespace osgEarth;
using namespace osgEarth::Util;
using namespace osgEarth::Procedural;

REGISTER_OSGEARTH_LAYER(biomes, BiomeLayer);

#define LC "[BiomeLayer] "

//...................................................................

void
BiomeLayer::Options::fromConfig(const Config& conf)
{
    biomeCatalog() = std::make_shared<BiomeCatalog>(conf.child("biomecatalog"));
    landCoverLayer().get(conf, "landcover_layer");
    biomeBaseLayer().get(conf, "biome_base_layer");
    conf.get("blend_radius", blendRadius());
    conf.get("blend_percentage", blendPercentage());
}

Config
BiomeLayer::Options::getConfig() const
{
    Config conf = ImageLayer::Options::getConfig();
    OE_DEBUG << LC << __func__ << " not yet implemented" << std::endl;
    landCoverLayer().set(conf, "landcover_layer");
    biomeBaseLayer().set(conf, "biome_base_layer");
    //TODO - biomeCatalog
    conf.set("blend_radius", blendRadius());
    conf.set("blend_percentage", blendPercentage());
    return conf;
}

//...................................................................

#undef LC
#define LC "[BiomeLayer] " << getName() << ": "

//namespace
//{
//    // Token for keeping track of biome images so we can page their
//    // asset data in and out
//    struct BiomeTrackerToken : public osg::Object
//    {
//        META_Object(osgEarth, BiomeTrackerToken);
//        BiomeTrackerToken() { }
//        BiomeTrackerToken(std::set<int>&& seen, BiomeLayer* layer) : _biome_index_set(seen), _biomeLayer(layer) { }
//        BiomeTrackerToken(const BiomeTrackerToken& rhs, const osg::CopyOp& op) { }
//        std::set<int> _biome_index_set;
//        osg::observer_ptr<BiomeLayer> _biomeLayer;
//    };
//}

BiomeLayer::BiomeTrackerToken::~BiomeTrackerToken()
{
    osg::ref_ptr<BiomeLayer> layer;
    if (_biomeLayer.lock(layer))
    {
        layer->trackerTokenDeleted(this);
    }
}

void
BiomeLayer::init()
{
    ImageLayer::init();

    // BiomeLayer is invisible AND shared by default.
    options().visible().setDefault(false);
    options().shared().setDefault(true);
    options().minFilter().setDefault(osg::Texture::NEAREST);
    options().magFilter().setDefault(osg::Texture::NEAREST);
    options().textureCompression().setDefault("none");

    _autoBiomeManagement = true;

    setProfile(Profile::create(Profile::GLOBAL_GEODETIC));
}

Status
BiomeLayer::openImplementation()
{
    Status p = ImageLayer::openImplementation();
    if (p.isError())
        return p;

    Status csStatus = options().biomeBaseLayer().open(getReadOptions());
    if (csStatus.isError())
        return csStatus;

    options().landCoverLayer().open(getReadOptions());

    // Warn the poor user if the configuration is missing
    if (getBiomeCatalog() == nullptr)
    {
        OE_WARN << LC << "No biome catalog found - could be trouble" << std::endl;
    }
    else if (getBiomeCatalog()->getAssets().empty())
    {
        OE_WARN << LC << "No asset catalog found - could be trouble" << std::endl;
    }

    return Status::OK();
}

Status
BiomeLayer::closeImplementation()
{
    options().landCoverLayer().close();
    options().biomeBaseLayer().close();

    // cache:
    {
        std::lock_guard<std::mutex> lock(_imageCache.mutex());
        _imageCache.clear();
    }

    return ImageLayer::closeImplementation();
}

void
BiomeLayer::addedToMap(const Map* map)
{
    options().landCoverLayer().addedToMap(map);
    options().biomeBaseLayer().addedToMap(map);

    if (!getLandCoverLayer() && !getBiomeBaseLayer())
    {
        setStatus(Status::ResourceUnavailable, "No source data available");
        return;
    }

    // Prepare to create samples for the base biome layer
    if (getBiomeBaseLayer() && getBiomeBaseLayer()->isOpen())
    {
        _biomeFactory = BiomeSample::Factory::create(getBiomeBaseLayer());
    }

    // Prepare to create samples for the landcover layer
    if (getLandCoverLayer() && getLandCoverLayer()->isOpen())
    {
        _landCoverFactory = LandCoverSample::Factory::create(getLandCoverLayer());
    }

    // Inherit data extents from the biome base layer and land cover layer...?
    DataExtentList dataExtents;

    if (getBiomeBaseLayer())
    {
        DataExtentList temp;
        getBiomeBaseLayer()->getDataExtents(temp);
        dataExtents.insert(dataExtents.end(), temp.begin(), temp.end());
    }

    if (getLandCoverLayer())
    {
        DataExtentList temp;
        getLandCoverLayer()->getDataExtents(temp);
        dataExtents.insert(dataExtents.end(), temp.begin(), temp.end());
    }

    setDataExtents(dataExtents);
}

void
BiomeLayer::removedFromMap(const Map* map)
{
    options().landCoverLayer().removedFromMap(map);
    options().biomeBaseLayer().removedFromMap(map);
}

CoverageLayer*
BiomeLayer::getLandCoverLayer() const
{
    return options().landCoverLayer().getLayer();
}

CoverageLayer*
BiomeLayer::getBiomeBaseLayer() const
{
    return options().biomeBaseLayer().getLayer();
}

std::shared_ptr<const BiomeCatalog>
BiomeLayer::getBiomeCatalog() const
{
    return options().biomeCatalog();
}

void
BiomeLayer::setAutoBiomeManagement(bool value)
{
    _autoBiomeManagement = value;

    if (_autoBiomeManagement == false)
    {
        _biomeMan.reset();
    }
}

bool
BiomeLayer::getAutoBiomeManagement() const
{
    return _autoBiomeManagement;
}

void
BiomeLayer::setBlendRadius(const Distance& value)
{
    options().blendRadius() = value;
    bumpRevision();

    std::lock_guard<std::mutex> lock(_imageCache.mutex());
    _imageCache.clear();
}

const Distance&
BiomeLayer::getBlendRadius() const
{
    return options().blendRadius().get();
}

void
BiomeLayer::setBlendPercentage(float value)
{
    options().blendPercentage() = clamp(value, 0.0f, 1.0f);
    bumpRevision();
}

float
BiomeLayer::getBlendPercentage() const
{
    return options().blendPercentage().get();
}

GeoImage
BiomeLayer::createImageImplementation(
    const TileKey& key,
    ProgressCallback* progress) const
{
    OE_PROFILING_ZONE;

    if (getBiomeCatalog() == nullptr)
    {
        return GeoImage::INVALID;
    }

#if 1
    // check the cache:
    {
        std::lock_guard<std::mutex> lock(_imageCache.mutex());

        auto iter = _imageCache.find(key);
        osg::ref_ptr<osg::Image> image;
        if (iter != _imageCache.end() && iter->second.lock(image))
        {
            return GeoImage(image.get(), key.getExtent());
        }
    }
#endif

    // allocate a 16-bit image so we can represent 32K biomes
    osg::ref_ptr<osg::Image> image = new osg::Image();
    image->allocateImage(
        getTileSize(),
        getTileSize(),
        1,
        GL_RED,
        GL_FLOAT);

    image->setInternalTextureFormat(GL_R16F);

    ImageUtils::PixelWriter write(image.get());
    osg::Vec4 value;
    float noise = 1.0f;

    // pseudo-random number generator:
    // too slow. use Random instead.
    //std::minstd_rand gen(key.hash());
    //std::uniform_real_distribution<double> prng;
    Random prng(key.hash());

    std::set<int> biome_indices_seen;
    const GeoExtent& ex = key.getExtent();
    GeoImage temp(image.get(), ex);
    GeoImageIterator iter(temp);
    std::unordered_set<std::string> missing_biomes;
    float radius = options().blendRadius()->as(ex.getSRS()->getUnits());

    // Use meta-tiling to read coverage data with access to the 
    // neighboring tiles - to support the blend radius.
    MetaTile<GeoCoverage<LandCoverSample>> landcoverData;

    if (_landCoverFactory)
    {
        auto creator = [&](const TileKey& key, ProgressCallback* p) {
            return _landCoverFactory->createCoverage(key, p);
        };
        landcoverData.setCreateTileFunction(creator);
        landcoverData.setCenterTileKey(key, progress);
    }

    // Use meta-tiling to read biome coverage data with access to the 
    // neighboring tiles
    MetaTile<GeoCoverage<BiomeSample>> biomeData;

    if (_biomeFactory)
    {
        auto creator = [&](const TileKey& key, ProgressCallback* p) {
            return _biomeFactory->createCoverage(key, p);
        };
        biomeData.setCreateTileFunction(creator);
        biomeData.setCenterTileKey(key, progress);
    }

    float biomeRadius = 0.0f;
    if (getBiomeBaseLayer())
    {
        auto bak = getBiomeBaseLayer()->getBestAvailableTileKey(key);
        if (bak.valid())
        {
            auto res = bak.getResolution(getTileSize());
            biomeRadius = getBlendPercentage() * res.second * (float)getTileSize();
        }
    }

    float landcoverRadius = 0.0f;
    if (getLandCoverLayer())
    {
        auto bak = getLandCoverLayer()->getBestAvailableTileKey(key);
        if (bak.valid())
        {
            auto res = bak.getResolution(getTileSize());
            landcoverRadius = getBlendPercentage() * res.second * (float)getTileSize();
        }
    }

    // Make a small l2 cache of biomes that we've already looked up and search the vector
    // before going to the catalog. This is a performance optimization to avoid repeated queries into a potentially large catalog.
    std::vector< const Biome* > biomeCache;
    auto getBiome = [&](const std::string& id) -> const Biome*
        {
            // Search the cache first
            for (auto i : biomeCache)
            {
                if (i->id() == id)
                {
                    return i;
                }
            }

            auto result = getBiomeCatalog()->getBiome(id);
            if (result)
            {
                biomeCache.push_back(result);
            }
            return result;
        };

    iter.forEachPixelOnCenter([&]()
        {
            const Biome* biome = nullptr;

            double x = iter.x();
            double y = iter.y();

            // First try the biome base layer
            if (biomeData.valid())
            {
                // randomly permute the coordinates in order to blend across biomes
                if (biomeRadius > temp.getUnitsPerPixel())
                {
                    x += biomeRadius * (prng.next() * 2.0 - 1.0);
                    y += biomeRadius * (prng.next() * 2.0 - 1.0);
                }

                // convert the x,y to u,v
                double u = (x - ex.xMin()) / ex.width();
                double v = (y - ex.yMin()) / ex.height();

                const BiomeSample* sample = biomeData.read(u, v);

                if (sample)
                {
                    if (sample->biomeid().isSet())
                    {
                        biome = getBiome(sample->biomeid().get());
                    }
                }
            }

            // Next try the landcover layer, which could either override the biome
            // completely (with a biomeid) or could alter the biome by filtering
            // with traits.
            if (landcoverData.valid())
            {
                // randomly permute the coordinates in order to blend across biomes
                if (landcoverRadius > temp.getUnitsPerPixel())
                {
                    x += landcoverRadius * (prng.next() * 2.0 - 1.0);
                    y += landcoverRadius * (prng.next() * 2.0 - 1.0);
                }

                // convert the x,y to u,v
                double u = (x - ex.xMin()) / ex.width();
                double v = (y - ex.yMin()) / ex.height();

                const LandCoverSample* sample = landcoverData.read(u, v);
                if (sample)
                {
                    // if the biomeid() is set, we are overriding the biome expressly:
                    if (sample->biomeid().isSet())
                    {
                        biome = getBiome(sample->biomeid().get());
                    }

                    // If we have a biome, but there are traits set, we need to
                    // find the implicit biome that incorporates those traits:
                    if (biome != nullptr && !sample->traits().empty())
                    {
                        std::vector<std::string> sorted = sample->traits();
                        if (sorted.size() > 1)
                            std::sort(sorted.begin(), sorted.end());

                        std::string implicit_biome_id =
                            biome->id() + "." + AssetTraits::toString(sorted);

                        const Biome* implicit_biome = getBiome(implicit_biome_id);

                        if (implicit_biome)
                        {
                            biome = implicit_biome;
                        }
                        else
                        {
                            missing_biomes.insert(implicit_biome_id);
                        }
                    }
                    // NB: lifemap values are handled by the LifeMapLayer (ignored here)
                }
            }

            // if we found a valid on
            int biome_index = biome ? biome->index() : 0;
            value.r() = (float)biome_index;
            write(value, iter.s(), iter.t());

            biome_indices_seen.insert(biome_index);
        });

    GeoImage result(image.get(), key.getExtent());

    // Set up tracking on all discovered biomes in this image. 
    // This will allow us to page out when all references to a biome
    // expire from the scene
    trackImage(result, key, biome_indices_seen);

    // report any errors
    if (!missing_biomes.empty())
    {
        std::ostringstream buf;
        buf << "Tile " << key.str() << " has biome(s) with no assets: ";
        for (auto i : missing_biomes)
            buf << i << ' ';
        OE_DEBUG << LC << buf.str() << std::endl;
    }

    // local cache:
    {
        std::lock_guard<std::mutex> lock(_imageCache.mutex());
        _imageCache[key] = image.get();
    }

    return result;
}

void
BiomeLayer::postCreateImageImplementation(
    GeoImage& createdImage,
    const TileKey& key,
    ProgressCallback* progress) const
{
    // (This runs post-caching)
    // When a new biome raster arrives, scan it to find a set of all
    // biome indices that it contains, and register this set with the
    // tracker.
    if (getAutoBiomeManagement() &&
        createdImage.getTrackingToken() == nullptr)
    {
        // if there's no tracking token (e.g., this image came from the cache)
        // build and attach one now.
        GeoImageIterator iter(createdImage);
        ImageUtils::PixelReader read(createdImage.getImage());

        std::set<int> biome_indices_seen;
        osg::Vec4 pixel;

        // known format (GL_RED/GL_FLOAT) - traverse manually for speed
        const osg::Image* image = createdImage.getImage();
        OE_IF_SOFT_ASSERT(image && image->getPixelFormat() == GL_RED && image->getDataType() == GL_FLOAT)
        {
            unsigned size = image->s() * image->t();
            const float* ptr = (const float*)(image->data());
            int biome_index;

            for (unsigned i = 0; i < size; ++i)
            {
                biome_index = (int)(*ptr++);
                if (biome_index > 0)
                    biome_indices_seen.insert(biome_index);
            }

            trackImage(createdImage, key, biome_indices_seen);
        }
    }
}

void
BiomeLayer::trackImage(
    GeoImage& image,
    const TileKey& key,
    std::set<int>& biome_index_set) const
{
    // inform the biome manager that we are using the biomes corresponding
    // to the set of biome indices collected from the raster.
    for (auto biome_index : biome_index_set)
    {
        const Biome* biome = getBiomeCatalog()->getBiomeByIndex(biome_index);
        if (biome)
            const_cast<BiomeManager*>(&_biomeMan)->ref(biome);
    }

    // Create a "token" object that we can track for destruction.
    // This will inform us when the image created by this call
    // destructs, and we can unref the usage in the BiomeManager accordingly.
    // This works, but reverses the flow of control, so maybe
    // there is a better solution -gw
    osg::Object* token = new BiomeTrackerToken(std::move(biome_index_set), const_cast<BiomeLayer*>(this));
    token->setName(Stringify() << "Biome token " << key.str());
    image.setTrackingToken(token);
    //token->addObserver(const_cast<BiomeLayer*>(this));
    _tracker.scoped_lock([&]() { _tracker[token] = key; });
}

void
BiomeLayer::trackerTokenDeleted(BiomeTrackerToken* token)
{
    // Invoked when the biome index raster destructs.
    // Inform the BiomeManager that the indices referenced by this
    // image are no longer in use (unreference the count).
    //BiomeTrackerToken* token = static_cast<BiomeTrackerToken*>(value);

    _tracker.scoped_lock([&]()
        {
            //OE_DEBUG << LC << "Unloaded " << token->getName() << std::endl;

            if (getAutoBiomeManagement())
            {
                for (auto biome_index : token->_biome_index_set)
                {
                    const Biome* biome = getBiomeCatalog()->getBiomeByIndex(biome_index);
                    if (biome)
                    {
                        _biomeMan.unref(biome);
                    }
                }
            }
            _tracker.erase(token);
        });
}

void
BiomeLayer::resizeGLObjectBuffers(unsigned maxsize)
{
    auto textures = _biomeMan.getTextures();
    if (textures)
        textures->resizeGLObjectBuffers(maxsize);
}

void
BiomeLayer::releaseGLObjects(osg::State* state) const
{
    auto textures = _biomeMan.getTextures();
    if (textures)
        textures->releaseGLObjects(state);
}
