/* osgEarth
 * Copyright 2025 Pelican Mapping
 * MIT License
 */
#include <osgEarth/FilterContext>
#include <osgEarth/Session>
#include <osgEarth/ResourceCache>
#include <osgEarth/FeatureSource>

using namespace osgEarth;

FilterContext::FilterContext(Session* session, const GeoExtent& workingExtent, FeatureIndexBuilder* index) :
    _session(session),
    _extent(workingExtent),
    _isGeocentric(false),
    _index(index),
    _shaderPolicy(osgEarth::SHADERPOLICY_GENERATE)
{
    //OE_SOFT_ASSERT(session, "ILLEGAL - session cannot be nullptr");

    _extent = workingExtent;

    if (session)
    {
        if (session->getResourceCache())
        {
            _resourceCache = session->getResourceCache();
        }
        else
        {
            _resourceCache = new ResourceCache();
        }

        if (session->getFeatureSource())
        {
            _profile = session->getFeatureSource()->getFeatureProfile();
        }
    }

    // attempt to establish a working extent if we don't have one:

    if (!_extent->isValid() &&
        _profile &&
        _profile->getExtent().isValid())
    {
        _extent = _profile->getExtent();
    }

    if (!_extent->isValid() &&
        session &&
        session->getMapProfile())
    {
        _extent = session->getMapProfile()->getExtent();
    }

    // if the session is set, push its name as the first bc.
    if (_session.valid())
    {
        pushHistory(_session->getName());
    }
}

FilterContext::FilterContext(Session* session, const FeatureProfile* profile, const GeoExtent& workingExtent, FeatureIndexBuilder* index) :
    _session(session),
    _profile(profile),
    _extent(workingExtent),
    _isGeocentric(false),
    _index(index),
    _shaderPolicy(osgEarth::SHADERPOLICY_GENERATE)
{
    OE_SOFT_ASSERT(session, "ILLEGAL - session cannot be nullptr");

    _extent = workingExtent;

    if (session)
    {
        if ( session->getResourceCache() )
        {
            _resourceCache = session->getResourceCache();
        }
        else
        {
            _resourceCache = new ResourceCache();
        }
    }

    // attempt to establish a working extent if we don't have one:

    if (!_extent->isValid() &&
        profile &&
        profile->getExtent().isValid() )
    {
        _extent = profile->getExtent();
    }

    if (!_extent->isValid() &&
        session && 
        session->getMapProfile() )
    {
        _extent = session->getMapProfile()->getExtent();
    }

    // if the session is set, push its name as the first bc.
    if ( _session.valid() )
    {
        pushHistory( _session->getName() );
    }
}

FilterContext::FilterContext(const FeatureProfile* profile, const Query& query)
{
    _profile = profile;

    if (query.tileKey().isSet())
        extent() = query.tileKey()->getExtent();
    else if (query.bounds().isSet() && profile)
        extent() = GeoExtent(profile->getSRS(), query.bounds().get());
    else if (profile)
        extent() = profile->getExtent();
}

void
FilterContext::setProfile(const FeatureProfile* value)
{
    _profile = value;
}

Session*
FilterContext::getSession()
{
    return _session.get();
}

const Session*
FilterContext::getSession() const
{
    return _session.get();
}

bool
FilterContext::isGeoreferenced() const
{ 
    return _session.valid() && _profile.valid();
}

const SpatialReference*
FilterContext::getOutputSRS() const
{
    if (_outputSRS.valid())
        return _outputSRS.get();

    if (_session.valid() && _session->getMapSRS())
        return _session->getMapSRS();

    if (_profile.valid() && _profile->getSRS())
        return _profile->getSRS();

    if (_extent.isSet())
        return _extent->getSRS();

    return SpatialReference::get("wgs84");
}

const osgDB::Options*
FilterContext::getDBOptions() const
{
    return _session.valid() ? _session->getDBOptions() : 0L;
}

void
FilterContext::toLocal( Geometry* geom ) const
{
    if ( hasReferenceFrame() )
    {
        GeometryIterator gi( geom );
        while( gi.hasMore() )
        {
            Geometry* g = gi.next();
            for( osg::Vec3dArray::iterator i = g->begin(); i != g->end(); ++i )
                *i = *i * _referenceFrame;
        }
    }
}

void
FilterContext::toWorld( Geometry* geom ) const
{
    if ( hasReferenceFrame() )
    {
        GeometryIterator gi( geom );
        while( gi.hasMore() )
        {
            Geometry* g = gi.next();
            for( osg::Vec3dArray::iterator i = g->begin(); i != g->end(); ++i )
                *i = *i * _inverseReferenceFrame;
        }
    }
}

osg::Vec3d
FilterContext::toMap( const osg::Vec3d& point ) const
{
    osg::Vec3d world = toWorld(point);
    osg::Vec3d map;
    _extent->getSRS()->transformFromWorld( world, map );
    return map;
}

osg::Vec3d
FilterContext::fromMap( const osg::Vec3d& point ) const
{
    osg::Vec3d world;
    _extent->getSRS()->transformToWorld( point, world );
    return toLocal(world);
}

ResourceCache*
FilterContext::resourceCache()
{
    return _resourceCache.get();
}

std::string
FilterContext::toString() const
{
    std::stringstream buf;

    buf << std::fixed
        << "CONTEXT: ["
        << "profile extent = "   << profile()->getExtent().toString()
        << ", working extent = " << extent()->toString()
        << ", geocentric = "     << osgEarth::toString(_isGeocentric)
        << ", history = "        << getHistory()
        << "]";

    std::string str;
    str = buf.str();
    return str;
}

std::string
FilterContext::getHistory() const
{
    std::stringstream buf;
    for(unsigned i=0; i<_history.size(); ++i)
    {
        if ( i > 0 ) buf << " : ";
        buf << _history[i];
    }
    return buf.str();
}
