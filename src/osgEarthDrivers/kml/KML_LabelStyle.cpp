/* -*-c++-*- */
/* osgEarth - Geospatial SDK for OpenSceneGraph
 * Copyright 2020 Pelican Mapping
 * http://osgearth.org
 *
 * osgEarth is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 */
#include "KML_LabelStyle"

using namespace osgEarth_kml;

void 
KML_LabelStyle::scan( xml_node<>* node, Style& style, KMLContext& cx )
{
    if (!node)
      return;
    TextSymbol* text = style.getOrCreate<TextSymbol>();
    std::string color = getValue(node, "color");
    if (!color.empty())
    {
      text->fill().mutable_value().color() = Color(Stringify() << "#" << color, Color::ABGR);
    } 
}
