/* osgEarth
* Copyright 2008-2014 Pelican Mapping
* MIT License
*/
#include "SelectionInfo"
#include <osgEarth/TileKey>

using namespace osgEarth::REX;
using namespace osgEarth;

#define LC "[SelectionInfo] "

const double SelectionInfo::_morphStartRatio = 0.66;

const SelectionInfo::LOD&
SelectionInfo::getLOD(unsigned lod) const
{
    static SelectionInfo::LOD s_dummy;

    if (lod-_firstLOD >= _lods.size())
    {
        // note, this can happen if firstLOD() is set
        return s_dummy;
    }
    return _lods[lod-_firstLOD];
}

void
SelectionInfo::initialize(unsigned firstLod, unsigned maxLod, const Profile* profile, double mtrf, bool restrictPolarSubdivision)
{
    OE_SOFT_ASSERT_AND_RETURN(profile != nullptr, void());
    OE_SOFT_ASSERT_AND_RETURN(profile->getSRS() != nullptr && profile->getSRS()->valid(), void());

    if (getNumLODs() > 0)
    {
        //OE_INFO << LC <<"Error: Selection Information already initialized"<<std::endl;
        return;
    }

    if (firstLod > maxLod)
    {
        OE_WARN << LC <<"Error: Inconsistent First and Max LODs"<<std::endl;
        return;
    }

    _firstLOD = firstLod;

    unsigned numLods = maxLod + 1u;

    _lods.resize(numLods);

    for (unsigned lod = 0; lod <= maxLod; ++lod)
    {
        unsigned tx, ty;
        profile->getNumTiles(lod, tx, ty);
        TileKey key(lod, tx/2, ty/2, profile);
        GeoExtent e = key.getExtent();
        GeoCircle c = e.computeBoundingGeoCircle();
        double range = c.getRadius() * mtrf * 2.0 * (1.0/1.405);
        _lods[lod]._visibilityRange = range;
        _lods[lod]._minValidTY = 0;
        _lods[lod]._maxValidTY = 0xFFFFFFFF;
    }
    
    double metersPerEquatorialDegree = (profile->getSRS()->getEllipsoid().getRadiusEquator() * 2.0 * osg::PI) / 360.0;

    double prevPos = 0.0;

    for (int lod=(int)(numLods-1); lod>=0; --lod)
    {
        double span = _lods[lod]._visibilityRange - prevPos;

        _lods[lod]._morphEnd   = _lods[lod]._visibilityRange;
        _lods[lod]._morphStart = prevPos + span*_morphStartRatio;
        //prevPos = _lods[i]._morphStart; // original value
        prevPos = _lods[lod]._morphEnd;

        // Calc the maximum valid TY (to avoid over-subdivision at the poles)
        // In a geographic map, this will effectively limit the maximum LOD
        // progressively starting at about +/- 72 degrees latitude.
        int startLOD = 6;
        if (restrictPolarSubdivision && lod >= startLOD && profile->getSRS()->isGeographic())
        {            
            const double startAR = 0.1; // minimum allowable aspect ratio at startLOD
            const double endAR = 0.4;   // minimum allowable aspect ratio at maxLOD
            double lodT = (double)(lod-startLOD)/(double)(numLods-1);
            double minAR = startAR + (endAR-startAR)*lodT;

            unsigned tx, ty;
            profile->getNumTiles(lod, tx, ty);
            for(int y=(int)ty/2; y>=0; --y)
            {
                TileKey k(lod, 0, y, profile);
                const GeoExtent& e = k.getExtent();
                double width_m = e.width(Units::METERS);
                double height_m = e.height(Units::METERS);
                if (width_m/height_m < minAR)
                {
                    _lods[lod]._minValidTY = std::min(y+1, (int)(ty-1));
                    _lods[lod]._maxValidTY = (ty-1)-_lods[lod]._minValidTY;
                    break;
                }
            }
        }
    }
}

void
SelectionInfo::get(const TileKey& key, 
                   float& out_range,
                   float& out_startMorphRange,
                   float& out_endMorphRange) const
{
    out_range = 0.0f;
    out_startMorphRange = 0.0f;
    out_endMorphRange = 0.0f;

    if (key.getLOD() < _lods.size())
    {
        const LOD& lod = _lods[key.getLOD()];

        if (key.getTileY() >= lod._minValidTY && key.getTileY() <= lod._maxValidTY)
        {
            out_range = lod._visibilityRange;
            out_startMorphRange = lod._morphStart;
            out_endMorphRange = lod._morphEnd;
        }
    }
}
