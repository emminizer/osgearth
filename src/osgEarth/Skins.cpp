/* osgEarth
 * Copyright 2025 Pelican Mapping
 * MIT License
 */
#include <osgEarth/Skins>
#include <osgEarth/Style>
#include <osgEarth/StringUtils>
#include <osgEarth/ImageUtils>
#include <osgEarth/Registry>

#include <osg/BlendFunc>
#include <osg/Texture2D>
#include <osg/Texture2DArray>

#include <osgEarth/ResourceLibrary>

#define LC "[SkinResource] "

using namespace osgEarth;

//---------------------------------------------------------------------------

SkinResource::SkinResource(const Config& conf) :
    Resource(conf)
{
    mergeConfig(conf);
}

void
SkinResource::mergeConfig(const Config& conf)
{
    conf.get("material", material());
    conf.get("url", imageURI());
    conf.get("image_width", _imageWidth);
    conf.get("image_height", _imageHeight);
    conf.get("min_object_height", minObjectHeight());
    conf.get("max_object_height", maxObjectHeight());
    conf.get("tiled", _isTiled);
    conf.get("max_texture_span", _maxTexSpan);

    conf.get("texture_mode", "decal", _texEnvMode, osg::TexEnv::DECAL);
    conf.get("texture_mode", "modulate", _texEnvMode, osg::TexEnv::MODULATE);
    conf.get("texture_mode", "replace", _texEnvMode, osg::TexEnv::REPLACE);
    conf.get("texture_mode", "blend", _texEnvMode, osg::TexEnv::BLEND);

    // texture atlas support
    conf.get("image_bias_s", _imageBiasS);
    conf.get("image_bias_t", _imageBiasT);
    conf.get("image_layer", _imageLayer);
    conf.get("image_scale_s", _imageScaleS);
    conf.get("image_scale_t", _imageScaleT);

    conf.get("atlas", _atlasHint);
    conf.get("read_options", _readOptions);
}

Config
SkinResource::getConfig() const
{
    Config conf = Resource::getConfig();
    conf.key() = "skin";

    conf.set("material", material());
    conf.set("url", imageURI());
    conf.set("image_width", _imageWidth);
    conf.set("image_height", _imageHeight);
    conf.set("min_object_height", minObjectHeight());
    conf.set("max_object_height", maxObjectHeight());
    conf.set("tiled", _isTiled);
    conf.set("max_texture_span", _maxTexSpan);

    conf.set("texture_mode", "decal", _texEnvMode, osg::TexEnv::DECAL);
    conf.set("texture_mode", "modulate", _texEnvMode, osg::TexEnv::MODULATE);
    conf.set("texture_mode", "replace", _texEnvMode, osg::TexEnv::REPLACE);
    conf.set("texture_mode", "blend", _texEnvMode, osg::TexEnv::BLEND);

    // texture atlas support
    conf.set("image_bias_s", _imageBiasS);
    conf.set("image_bias_t", _imageBiasT);
    conf.set("image_layer", _imageLayer);
    conf.set("image_scale_s", _imageScaleS);
    conf.set("image_scale_t", _imageScaleT);

    conf.set("atlas", _atlasHint);
    conf.set("read_options", _readOptions);

    return conf;
}

std::string
SkinResource::getUniqueID() const
{
    return
        imageURI().isSet() ? imageURI()->full() :
        material().isSet() ? material()->color()->full() :
        std::string{};
}

osg::StateAttribute*
SkinResource::createStateAttribute(const osgDB::Options* readOptions) const
{
    if (material().isSet())
    {
        osg::ref_ptr<PBRTexture> pbr_texture = new PBRTexture();
        auto status = pbr_texture->load(material().value(), readOptions);
        if (!status.isOK())
        {
            OE_WARN << LC << "One or more errors loading material for skin " << name() << std::endl;
            return nullptr;
        }

        if (isTiled() == true)
        {
            pbr_texture->setWrap(osg::Texture::WRAP_S, osg::Texture::REPEAT);
            pbr_texture->setWrap(osg::Texture::WRAP_T, osg::Texture::REPEAT);
        }

        return pbr_texture.release();
    }

    else
    {
        osg::ref_ptr<osg::Image> image = createColorImage(readOptions);
        return createTexture(image.get());
    }
}

osg::Texture*
SkinResource::createTexture(osg::Image* image) const
{
    if ( !image ) return 0L;

    osg::Texture* tex;

    if (image->r() > 1)
    {
        osg::Texture2DArray* ta = new osg::Texture2DArray();

        ta->setTextureDepth(image->r());
        ta->setTextureWidth(image->s());
        ta->setTextureHeight(image->t());
        ta->setInternalFormatMode(osg::Texture::USE_IMAGE_DATA_FORMAT);
        tex = ta;

        std::vector<osg::ref_ptr<osg::Image> > layers;
        ImageUtils::flattenImage(image, layers);
        for (unsigned i = 0; i < layers.size(); ++i)
        {
            tex->setImage(i, const_cast<osg::Image*>(layers[i].get()));
        }
        tex->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
        tex->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
    }
    else
    {
        tex = new osg::Texture2D(image);
        tex->setWrap(osg::Texture::WRAP_S, osg::Texture::REPEAT);
        tex->setWrap(osg::Texture::WRAP_T, osg::Texture::REPEAT);
    }

    tex->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR_MIPMAP_LINEAR);
    tex->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);

    // skin textures are likely to be shared, paged, etc. so keep them in memory.
    tex->setUnRefImageDataAfterApply(false);

    // don't resize them, let it be
    tex->setResizeNonPowerOfTwoHint(false);

	// JB:  The image could be shared and it's possible that mipmaps could be generated at the same time in different threads.
	// Need to figure out a safe place to call generateMipMaps, or just rely on baking them into the image beforehand as an art step.
    //ImageUtils::generateMipmaps(tex);

    return tex;
}

osg::StateSet*
SkinResource::createStateSet(const osgDB::Options* readOptions) const
{
    OE_DEBUG << LC << "Creating skin state set for " << imageURI()->full() << std::endl;

    auto stateset = new osg::StateSet();
    osg::Texture* albedo_texture = nullptr;

    auto sa = createStateAttribute(readOptions);

    auto pbr_texture = dynamic_cast<PBRTexture*>(sa);
    if (pbr_texture)
        albedo_texture = pbr_texture->albedo.get();
    else
        albedo_texture = dynamic_cast<osg::Texture*>(sa);

    if (sa)
    {
        stateset->setTextureAttributeAndModes(0, sa, osg::StateAttribute::ON);
    }

    if (pbr_texture)
    {
        stateset->setTextureAttributeAndModes(0, pbr_texture, osg::StateAttribute::ON);
    }

    if (_texEnvMode.isSet())
    {
        osg::TexEnv* texenv = new osg::TexEnv();
        texenv->setMode(*_texEnvMode);
        stateset->setTextureAttributeAndModes(0, texenv, osg::StateAttribute::ON);
    }

    if (albedo_texture && ImageUtils::hasAlphaChannel(albedo_texture->getImage(0)))
    {
        auto* blendFunc = new osg::BlendFunc();
        blendFunc->setFunction(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        stateset->setAttributeAndModes(blendFunc, osg::StateAttribute::ON);
        stateset->setRenderingHint(osg::StateSet::TRANSPARENT_BIN);
    }

    return stateset;
}

osg::ref_ptr<osg::Image>
SkinResource::createColorImage( const osgDB::Options* dbOptions ) const
{
    if (getStatus().isError())
        return 0L;

    ReadResult result;
    if (_readOptions.isSet())
    {
        osg::ref_ptr<osgDB::Options> ro = Registry::cloneOrCreateOptions(dbOptions);
        ro->setOptionString(Stringify() << _readOptions.get() << " " << ro->getOptionString());
        result = imageURI()->readImage(ro.get());
    }
    else
    {
        result = imageURI()->readImage(dbOptions);
    }

    if (result.failed())
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_status.isOK())
            _status = Status::Error(Status::ServiceUnavailable, "Failed to load resource image\n");
    }
    return result.releaseImage();
}

//---------------------------------------------------------------------------

OSGEARTH_REGISTER_SIMPLE_SYMBOL(skin, SkinSymbol);

SkinSymbol::SkinSymbol(const SkinSymbol& rhs, const osg::CopyOp& copyop) :
    TaggableWithConfig<Symbol>(rhs, copyop),
    _library(rhs._library),
    _objHeight(rhs._objHeight),
    _minObjHeight(rhs._minObjHeight),
    _maxObjHeight(rhs._maxObjHeight),
    _isTiled(rhs._isTiled),
    _randomSeed(rhs._randomSeed),
    _name(rhs._name)
{
}

SkinSymbol::SkinSymbol(const Config& conf) :
    TaggableWithConfig<Symbol>(conf),
    _objHeight(0.0f),
    _minObjHeight(0.0f),
    _maxObjHeight(FLT_MAX),
    _isTiled(false),
    _randomSeed(0)
{
    if (!conf.empty())
        mergeConfig(conf);
}

void
SkinSymbol::mergeConfig( const Config& conf )
{
    conf.get( "object_height",       _objHeight );
    conf.get( "min_object_height",   _minObjHeight );
    conf.get( "max_object_height",   _maxObjHeight );
    conf.get( "tiled",               _isTiled );
    conf.get( "random_seed",         _randomSeed );
    conf.get( "name",                _name );

    addTags( conf.value("tags" ) );
}

Config 
SkinSymbol::getConfig() const
{
    Config conf = Symbol::getConfig();
    conf.key() = "skin";

    conf.set( "object_height",       _objHeight );
    conf.set( "min_object_height",   _minObjHeight );
    conf.set( "max_object_height",   _maxObjHeight );
    conf.set( "tiled",               _isTiled );
    conf.set( "random_seed",         _randomSeed );
    conf.set( "name",                _name );

    std::string tagstring = this->tagString();
    if ( !tagstring.empty() )
        conf.set("tags", tagstring);

    return conf;
}


void
SkinSymbol::parseSLD(const Config& c, Style& style)
{
    if (match(c.key(), "library")) {
        if (!c.value().empty())
            style.getOrCreate<SkinSymbol>()->library() = Strings::unquote(c.value());
    }
    else
    if ( match(c.key(), "skin-library")) {
        if ( !c.value().empty() ) 
            style.getOrCreate<SkinSymbol>()->library() = Strings::unquote(c.value());
    }
    else if ( match(c.key(), "skin-tags") ) {
        style.getOrCreate<SkinSymbol>()->addTags( c.value() );
    }
    else if ( match(c.key(), "skin-tiled") ) {
        style.getOrCreate<SkinSymbol>()->isTiled() = as<bool>( c.value(), false );
    }
    else if ( match(c.key(), "skin-object-height") ) {
        style.getOrCreate<SkinSymbol>()->objectHeight() = as<float>( c.value(), 0.0f );
    }
    else if (match(c.key(), "skin-min-object-height") ) {
        style.getOrCreate<SkinSymbol>()->minObjectHeight() = as<float>( c.value(), 0.0f );
    }
    else if (match(c.key(), "skin-max-object-height") ) {
        style.getOrCreate<SkinSymbol>()->maxObjectHeight() = as<float>( c.value(), 0.0f );
    }
    else if (match(c.key(), "skin-random-seed") ) {
        style.getOrCreate<SkinSymbol>()->randomSeed() = as<unsigned>( c.value(), 0u );
    }
    else if (match(c.key(), "skin") || match(c.key(), "skin-name")) {
        style.getOrCreate<SkinSymbol>()->name() = StringExpression(c.value());
    }
}

SkinResource*
SkinSymbol::getResource(ResourceLibrary* lib, unsigned rnd, const osgDB::Options* readOptions) const
{
    SkinResource* result = nullptr;

    if (lib)
    {
        result = lib->getSkin(this, rnd, readOptions);
    }

    if (!result && name().isSet())
    {
        result = new SkinResource();
        result->imageURI() = URI(name()->eval(), uriContext());
    }

    return result;
}
