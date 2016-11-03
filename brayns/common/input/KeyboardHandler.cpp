/* Copyright (c) 2015-2016, EPFL/Blue Brain Project
 * All rights reserved. Do not distribute without permission.
 * Responsible Author: Cyrille Favreau <cyrille.favreau@epfl.ch>
 *
 * This file is part of Brayns <https://github.com/BlueBrain/Brayns>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 3.0 as published
 * by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "KeyboardHandler.h"

#include <brayns/parameters/ParametersManager.h>
#include <brayns/parameters/RenderingParameters.h>
#include <brayns/parameters/SceneParameters.h>
#include <brayns/parameters/VolumeParameters.h>
#include <brayns/common/scene/Scene.h>

#include <sstream>

namespace brayns
{

KeyboardHandler::KeyboardHandler(
    ScenePtr scene,
    ParametersManagerPtr parametersManager )
    : _parametersManager( parametersManager )
    , _scene( scene )
{
}

void KeyboardHandler::registerKeyboardShortcut(
    const unsigned char key,
    const std::string& description,
    std::function< void() > functor )
{
    if( _registeredShortcuts.find( key ) != _registeredShortcuts.end() )
    {
        std::stringstream message;
        message << key << " is already registered";
        BRAYNS_ERROR << message.str() << std::endl;
    }
    else
    {
        ShortcutInformation shortcutInformation = { description, functor };
        _registeredShortcuts[ key ] = shortcutInformation;
    }
}

void KeyboardHandler::unregisterKeyboardShortcut( const unsigned char key )
{
    auto it = _registeredShortcuts.find( key );
    if( it != _registeredShortcuts.end() )
        _registeredShortcuts.erase( it );
}

void KeyboardHandler::handleKeyboardShortcut( const unsigned char key )
{
    auto it = _registeredShortcuts.find( key );
    if( it != _registeredShortcuts.end() )
    {
        BRAYNS_INFO << "Processing " << (*it).second.description << std::endl;
        (*it).second.functor();
    }
}

std::string KeyboardHandler::help()
{
    std::stringstream result;
    result << "Keyboard shortcuts:" << std::endl;
    for( const auto& registeredShortcut: _registeredShortcuts )
        result << "'" << registeredShortcut.first
               << "' " + registeredShortcut.second.description << "\n";
    return result.str();
}

}