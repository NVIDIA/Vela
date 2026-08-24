-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
-- SPDX-License-Identifier: Apache-2.0

---@meta

-- LuaLS stub file for the VSR scripting API
-- Generated from Sol2 bindings in vsr/src/vsr/scripting/bindings/

------------------------------------------------------------------------
-- Math types (MathBindings.cpp)
------------------------------------------------------------------------

---@class vsr.float2
---@field x number
---@field y number
---@operator add(vsr.float2): vsr.float2
---@operator sub(vsr.float2): vsr.float2
---@operator mul(vsr.float2): vsr.float2
---@operator mul(number): vsr.float2
---@operator div(vsr.float2): vsr.float2
---@operator div(number): vsr.float2
---@operator unm: vsr.float2
local float2 = {}

---@class vsr.float3
---@field x number
---@field y number
---@field z number
---@operator add(vsr.float3): vsr.float3
---@operator sub(vsr.float3): vsr.float3
---@operator mul(vsr.float3): vsr.float3
---@operator mul(number): vsr.float3
---@operator div(vsr.float3): vsr.float3
---@operator div(number): vsr.float3
---@operator unm: vsr.float3
local float3 = {}

---@class vsr.float4
---@field x number
---@field y number
---@field z number
---@field w number
---@operator add(vsr.float4): vsr.float4
---@operator sub(vsr.float4): vsr.float4
---@operator mul(vsr.float4): vsr.float4
---@operator mul(number): vsr.float4
---@operator div(vsr.float4): vsr.float4
---@operator div(number): vsr.float4
---@operator unm: vsr.float4
local float4 = {}

---@class vsr.mat3
---@field identity vsr.mat3
---@operator index(integer): vsr.float3
local mat3 = {}

---@class vsr.mat4
---@field identity vsr.mat4
---@operator index(integer): vsr.float4
---@operator mul(vsr.mat4): vsr.mat4
---@operator mul(vsr.float4): vsr.float4
local mat4 = {}

------------------------------------------------------------------------
-- Parameter value types accepted by setParameter / returned by getParameter
------------------------------------------------------------------------

---@alias vsr.ParameterValue boolean|number|string|vsr.float2|vsr.float3|vsr.float4|vsr.mat4|vsr.Array|vsr.Sampler|vsr.Geometry|vsr.Material|vsr.SpatialField|vsr.Volume|vsr.Light|vsr.Camera|vsr.Surface|number[]

------------------------------------------------------------------------
-- Token (ContextBindings.cpp)
------------------------------------------------------------------------

---@class vsr.Token
local Token = {}

---@overload fun(): vsr.Token
---@overload fun(str: string): vsr.Token
---@return vsr.Token
function Token.new(...) end

---@return string
function Token:str() end

---@return boolean
function Token:empty() end

------------------------------------------------------------------------
-- Parameter (ContextBindings.cpp — read-only descriptor)
------------------------------------------------------------------------

---@class vsr.Parameter
local Parameter = {}

---@return string
function Parameter:name() end

---@return string
function Parameter:description() end

---@return boolean
function Parameter:isEnabled() end

------------------------------------------------------------------------
-- Object (ObjectMethodBindings.hpp — base type for all scene objects)
------------------------------------------------------------------------

---@class vsr.Object
---@field name string # Object name (read/write)
local Object = {}

---@return string
function Object:subtype() end

---@return integer
function Object:type() end

---@return integer
function Object:index() end

--- Set a parameter value on this object.
---@param name string
---@param value vsr.ParameterValue
function Object:setParameter(name, value) end

--- Create an array, populate it from Lua data, and bind it to a parameter.
--- For 2D/3D arrays, `data` may be linear or shape-matching nested.
--- `typeStr` may also name ANARI object arrays such as "geometry" or "material".
---@param name string
---@param typeStr string
---@param data table
---@overload fun(self: vsr.Object, name: string, typeStr: string, items0: integer, data: table): vsr.Array
---@overload fun(self: vsr.Object, name: string, typeStr: string, items0: integer, items1: integer, data: table): vsr.Array
---@overload fun(self: vsr.Object, name: string, typeStr: string, items0: integer, items1: integer, items2: integer, data: table): vsr.Array
---@return vsr.Array
function Object:setParameterArray(name, typeStr, data) end

--- Get the Parameter descriptor for the named parameter.
---@param name string
---@return vsr.Parameter?
function Object:parameter(name) end

--- Get the current value of a parameter.
---@param name string
---@return vsr.ParameterValue?
function Object:getParameter(name) end

---@param name string
function Object:removeParameter(name) end

function Object:removeAllParameters() end

---@return integer
function Object:numParameters() end

---@param index integer
---@return string
function Object:parameterNameAt(index) end

--- Set a metadata value on this object.
---@param key string
---@param value boolean|number|string
function Object:setMetadata(key, value) end

--- Get a metadata value from this object.
---@param key string
---@return boolean|number|string|nil
function Object:getMetadata(key) end

---@param key string
function Object:removeMetadata(key) end

---@return integer
function Object:numMetadata() end

---@param index integer
---@return string
function Object:getMetadataName(index) end

------------------------------------------------------------------------
-- Object types (ObjectBindings.cpp)
------------------------------------------------------------------------

---@class vsr.Geometry: vsr.Object
local Geometry = {}
---@return boolean
function Geometry:valid() end

---@class vsr.Material: vsr.Object
local Material = {}
---@return boolean
function Material:valid() end

---@class vsr.Light: vsr.Object
local Light = {}
---@return boolean
function Light:valid() end

---@class vsr.Camera: vsr.Object
local Camera = {}
---@return boolean
function Camera:valid() end

---@class vsr.Sampler: vsr.Object
local Sampler = {}
---@return boolean
function Sampler:valid() end

---@class vsr.Surface: vsr.Object
local Surface = {}
---@return boolean
function Surface:valid() end

--- Get the geometry attached to this surface.
---@return vsr.Geometry?
function Surface:geometry() end

--- Get the material attached to this surface.
---@return vsr.Material?
function Surface:material() end

---@class vsr.Volume: vsr.Object
local Volume = {}
---@return boolean
function Volume:valid() end

--- Get the spatial field attached to this volume.
---@return vsr.SpatialField?
function Volume:spatialField() end

---@class vsr.SpatialField: vsr.Object
local SpatialField = {}
---@return boolean
function SpatialField:valid() end

--- Compute the value range of this spatial field.
---@return vsr.float2
function SpatialField:computeValueRange() end

---@class vsr.Array: vsr.Object
local Array = {}
---@return boolean
function Array:valid() end

---@return integer
function Array:elementType() end

---@return integer
function Array:size() end

---@return integer
function Array:elementSize() end

---@return boolean
function Array:isEmpty() end

---@param d integer
---@return integer
function Array:dim(d) end

--- Set array data from a Lua table.
--- For 2D/3D arrays, supports either a flat/linear table or a shape-matching nested table.
--- Vector elements support `vsr.float2/3/4(...)` values or numeric tables.
---@param data table
function Array:setData(data) end

--- Get array data as a flat/linear Lua table.
---@return table
function Array:getData() end

------------------------------------------------------------------------
-- Layer types (LayerBindings.cpp)
------------------------------------------------------------------------

---@class vsr.LayerNode
---@field name string # Node name (read/write)
local LayerNode = {}

---@return boolean
function LayerNode:valid() end

---@return integer
function LayerNode:index() end

---@return vsr.LayerNode
function LayerNode:parent() end

---@return vsr.LayerNode
function LayerNode:next() end

---@return vsr.LayerNode
function LayerNode:sibling() end

---@return boolean
function LayerNode:isRoot() end

---@return boolean
function LayerNode:isLeaf() end

--- Get the Nth direct child (0-based). Returns invalid node if out of range.
---@param index integer
---@return vsr.LayerNode
function LayerNode:child(index) end

--- Find the first direct child with the given name.
---@param name string
---@return vsr.LayerNode
function LayerNode:childByName(name) end

---@return boolean
function LayerNode:isObject() end

---@return boolean
function LayerNode:isTransform() end

---@return boolean
function LayerNode:isEmpty() end

---@return boolean
function LayerNode:isEnabled() end

---@param enabled boolean
function LayerNode:setEnabled(enabled) end

---@return vsr.mat4
function LayerNode:getTransform() end

--- Get transform as packed SRT (columns: scale, euler-rotation-degrees, translation).
---@return vsr.mat3
function LayerNode:getTransformSRT() end

---@overload fun(self: vsr.LayerNode, m: vsr.mat4)
---@overload fun(self: vsr.LayerNode, srt: vsr.mat3)
function LayerNode:setAsTransform(m) end

--- Set node as a transform array (array of mat4 for multi-instancing).
---@param a vsr.Array
function LayerNode:setAsTransformArray(a) end

--- Get the transform array, or nil if this node is not a transform array.
---@return vsr.Array|nil
function LayerNode:getTransformArray() end

---@class vsr.Layer
local Layer = {}

---@return vsr.LayerNode
function Layer:root() end

---@return integer
function Layer:size() end

---@return boolean
function Layer:empty() end

---@param index integer
---@return vsr.LayerNode
function Layer:at(index) end

--- Traverse the layer tree. Callback receives (node, level). Return false to stop.
---@param fn fun(node: vsr.LayerNode, level: integer): boolean?
function Layer:foreach(fn) end

------------------------------------------------------------------------
-- Animation (CoreBindings.cpp)
------------------------------------------------------------------------

---@class vsr.Animation
---@field name string # Animation name (read/write)
local Animation = {}

--- Add a parameter binding to this animation.
---@param target vsr.Object
---@param param string
---@param dataType string # e.g. "float3", "spatialField"
---@param data table # table of values or object refs
---@param timeBase number[] # table of float timestamps
---@param interp? string # "linear" (default), "step", or "slerp"
function Animation:addObjectParameterBinding(target, param, dataType, data, timeBase, interp) end

--- Add a transform binding to this animation.
---@param node vsr.LayerNode
---@param timeBase number[] # table of float timestamps
---@param rotation table # table of float4 quaternions
---@param translation table # table of float3 positions
---@param scale table # table of float3 scale factors
function Animation:addTransformBinding(node, timeBase, rotation, translation, scale) end

------------------------------------------------------------------------
-- Scene (ContextBindings.cpp)
------------------------------------------------------------------------

---@class vsr.Scene
local Scene = {}

---@return vsr.Scene
function Scene.new() end

-- Object creation --------------------------------------------------------

---@param subtype string
---@param params? table<string, vsr.ParameterValue>
---@return vsr.Geometry
function Scene:createGeometry(subtype, params) end

---@param subtype string
---@param params? table<string, vsr.ParameterValue>
---@return vsr.Material
function Scene:createMaterial(subtype, params) end

---@param subtype string
---@param params? table<string, vsr.ParameterValue>
---@return vsr.Light
function Scene:createLight(subtype, params) end

---@param subtype string
---@param params? table<string, vsr.ParameterValue>
---@return vsr.Camera
function Scene:createCamera(subtype, params) end

---@param subtype string
---@param params? table<string, vsr.ParameterValue>
---@return vsr.Sampler
function Scene:createSampler(subtype, params) end

---@param subtype string
---@param params? table<string, vsr.ParameterValue>
---@return vsr.Volume
function Scene:createVolume(subtype, params) end

---@param subtype string
---@param params? table<string, vsr.ParameterValue>
---@return vsr.SpatialField
function Scene:createSpatialField(subtype, params) end

---@param name string
---@param geometry vsr.Geometry
---@param material vsr.Material
---@param params? table<string, vsr.ParameterValue>
---@return vsr.Surface
function Scene:createSurface(name, geometry, material, params) end

--- Create a typed array. Valid type strings:
--- "float", "float2", "float3", "float4",
--- "int", "int2", "int3", "int4",
--- "uint", "uint2", "uint3", "uint4",
--- "mat4",
--- "spatialField", "geometry", "material", "surface", "volume",
--- "light", "camera", "sampler", "array1d".
--- Pass a table to populate inline; table shape determines dimensions
--- unless explicit sizes are given.
---@param typeStr string
---@param items0_or_data integer|table
---@overload fun(self: vsr.Scene, typeStr: string, data: table): vsr.Array
---@overload fun(self: vsr.Scene, typeStr: string, items0: integer): vsr.Array
---@overload fun(self: vsr.Scene, typeStr: string, items0: integer, data: table): vsr.Array
---@overload fun(self: vsr.Scene, typeStr: string, items0: integer, items1: integer): vsr.Array
---@overload fun(self: vsr.Scene, typeStr: string, items0: integer, items1: integer, data: table): vsr.Array
---@overload fun(self: vsr.Scene, typeStr: string, items0: integer, items1: integer, items2: integer): vsr.Array
---@overload fun(self: vsr.Scene, typeStr: string, items0: integer, items1: integer, items2: integer, data: table): vsr.Array
---@return vsr.Array
function Scene:createArray(typeStr, items0_or_data) end

-- Object access ----------------------------------------------------------

---@param index integer
---@return vsr.Geometry
function Scene:getGeometry(index) end

---@param index integer
---@return vsr.Material
function Scene:getMaterial(index) end

---@param index integer
---@return vsr.Light
function Scene:getLight(index) end

---@param index integer
---@return vsr.Camera
function Scene:getCamera(index) end

---@param index integer
---@return vsr.Surface
function Scene:getSurface(index) end

---@param index integer
---@return vsr.Array
function Scene:getArray(index) end

---@param index integer
---@return vsr.Volume
function Scene:getVolume(index) end

---@param index integer
---@return vsr.Sampler
function Scene:getSampler(index) end

---@param index integer
---@return vsr.SpatialField
function Scene:getSpatialField(index) end

-- Object counts ----------------------------------------------------------

--- Return the number of objects of the given ANARI type.
---@param type integer # Use vsr.GEOMETRY, vsr.MATERIAL, etc.
---@return integer
function Scene:numberOfObjects(type) end

-- Iteration --------------------------------------------------------------

--- Iterate over all geometries. Return false from the callback to stop early.
---@param fn fun(obj: vsr.Geometry): boolean?
function Scene:forEachGeometry(fn) end

--- Iterate over all materials. Return false from the callback to stop early.
---@param fn fun(obj: vsr.Material): boolean?
function Scene:forEachMaterial(fn) end

--- Iterate over all surfaces. Return false from the callback to stop early.
---@param fn fun(obj: vsr.Surface): boolean?
function Scene:forEachSurface(fn) end

--- Iterate over all lights. Return false from the callback to stop early.
---@param fn fun(obj: vsr.Light): boolean?
function Scene:forEachLight(fn) end

--- Iterate over all cameras. Return false from the callback to stop early.
---@param fn fun(obj: vsr.Camera): boolean?
function Scene:forEachCamera(fn) end

--- Iterate over all volumes. Return false from the callback to stop early.
---@param fn fun(obj: vsr.Volume): boolean?
function Scene:forEachVolume(fn) end

--- Iterate over all spatial fields. Return false from the callback to stop early.
---@param fn fun(obj: vsr.SpatialField): boolean?
function Scene:forEachSpatialField(fn) end

--- Iterate over all samplers. Return false from the callback to stop early.
---@param fn fun(obj: vsr.Sampler): boolean?
function Scene:forEachSampler(fn) end

--- Iterate over all arrays. Return false from the callback to stop early.
---@param fn fun(obj: vsr.Array): boolean?
function Scene:forEachArray(fn) end

-- Layers -----------------------------------------------------------------

---@param name string
---@return vsr.Layer
function Scene:addLayer(name) end

---@overload fun(self: vsr.Scene, name: string): vsr.Layer
---@overload fun(self: vsr.Scene, index: integer): vsr.Layer
---@return vsr.Layer
function Scene:layer(...) end

---@return integer
function Scene:numberOfLayers() end

---@return vsr.Layer
function Scene:defaultLayer() end

---@return vsr.Material
function Scene:defaultMaterial() end

---@param name string
---@return boolean
function Scene:layerIsActive(name) end

---@param name string
---@param active boolean
function Scene:setLayerActive(name, active) end

function Scene:setAllLayersActive() end

--- Deactivate all layers, then activate only the named layer.
---@param name string
function Scene:setOnlyLayerActive(name) end

---@return integer
function Scene:numberOfActiveLayers() end

--- Signal that a layer structure has changed
---@param layer vsr.Layer
function Scene:signalLayerStructureChanged(layer) end

--- Signal that a layer has changed transforms
---@param layer vsr.Layer
function Scene:signalLayerTransformChanged(layer) end

-- Node insertion ---------------------------------------------------------

---@param parent vsr.LayerNode
---@param name string
---@return vsr.LayerNode
function Scene:insertChildNode(parent, name) end

---@param parent vsr.LayerNode
---@param transform vsr.mat4
---@param name string
---@return vsr.LayerNode
function Scene:insertChildTransformNode(parent, transform, name) end

--- Insert a child node with an array of mat4 transforms (multi-instancing).
---@param parent vsr.LayerNode
---@param array vsr.Array # Array of FLOAT32_MAT4
---@param name string
---@return vsr.LayerNode
function Scene:insertChildTransformArrayNode(parent, array, name) end

--- Insert an object (surface, light, or volume) into the scene graph.
---@param parent vsr.LayerNode
---@param obj vsr.Surface|vsr.Light|vsr.Volume
---@param name? string
---@return vsr.LayerNode
function Scene:insertObjectNode(parent, obj, name) end

-- Object removal ---------------------------------------------------------

---@param obj vsr.Object
function Scene:removeObject(obj) end

function Scene:removeAllObjects() end

---@overload fun(self: vsr.Scene, name: string)
---@overload fun(self: vsr.Scene, layer: vsr.Layer)
function Scene:removeLayer(...) end

function Scene:removeAllLayers() end

--- Remove a node from the scene graph.
---@overload fun(self: vsr.Scene, node: vsr.LayerNode)
---@overload fun(self: vsr.Scene, node: vsr.LayerNode, deleteObjects: boolean)
function Scene:removeNode(...) end

-- Cleanup ----------------------------------------------------------------

function Scene:removeUnusedObjects() end

function Scene:defragmentObjectStorage() end

function Scene:cleanupScene() end

------------------------------------------------------------------------
-- AnimationManager (CoreBindings.cpp)
------------------------------------------------------------------------

---@class vsr.AnimationManager
local AnimationManager = {}

---@overload fun(self: vsr.AnimationManager): vsr.Animation
---@overload fun(self: vsr.AnimationManager, name: string): vsr.Animation
---@return vsr.Animation
function AnimationManager:addAnimation(...) end

---@return vsr.Animation[]
function AnimationManager:animations() end

---@return integer
function AnimationManager:numberOfAnimations() end

---@param index integer
function AnimationManager:removeAnimation(index) end

function AnimationManager:removeAllAnimations() end

---@param time number
function AnimationManager:setAnimationTime(time) end

---@return number
function AnimationManager:getAnimationTime() end

---@param increment number
function AnimationManager:setAnimationIncrement(increment) end

---@return number
function AnimationManager:getAnimationIncrement() end

function AnimationManager:incrementAnimationTime() end

---@return integer
function AnimationManager:getAnimationTotalFrames() end

---@param frames integer
function AnimationManager:setAnimationTotalFrames(frames) end

---@return number
function AnimationManager:getAnimationFPS() end

---@param fps number
function AnimationManager:setAnimationFPS(fps) end

---@return integer
function AnimationManager:getAnimationFrame() end

---@param frame integer
function AnimationManager:setAnimationFrame(frame) end

function AnimationManager:incrementAnimationFrame() end

------------------------------------------------------------------------
-- Render types (RenderBindings.cpp)
------------------------------------------------------------------------

---@class vsr.CameraSetup
---@field position vsr.float3
---@field direction vsr.float3
---@field up vsr.float3
---@field fovy number
---@field aspect number
---@field aperture number   # Aperture radius for depth of field (0 = disabled)
---@field focusDistance number  # Focus distance for depth of field
local CameraSetup = {}

---@return vsr.CameraSetup
function CameraSetup.new() end

---@class vsr.AnariDevice
---@field libraryName string # (read-only)

---@class vsr.RenderIndex
local RenderIndex = {}

---@overload fun(scene: vsr.Scene, device: any): vsr.RenderIndex
---@return vsr.RenderIndex
function RenderIndex.new(...) end

--- Bootstrap or rebuild this render index from the current scene snapshot.
--- This does not register the render index for live scene updates.
function RenderIndex:populate() end

---@return any
function RenderIndex:world() end

---@return any
function RenderIndex:device() end

---@class vsr.ImagePipeline
local ImagePipeline = {}

---@overload fun(): vsr.ImagePipeline
---@overload fun(width: integer, height: integer): vsr.ImagePipeline
---@return vsr.ImagePipeline
function ImagePipeline.new(...) end

---@param width integer
---@param height integer
function ImagePipeline:setDimensions(width, height) end

function ImagePipeline:render() end

---@return integer
function ImagePipeline:size() end

---@return boolean
function ImagePipeline:empty() end

function ImagePipeline:clear() end

------------------------------------------------------------------------
-- Module-level table (injected as a global by the C++ runtime)
------------------------------------------------------------------------

---@class vsr
---@diagnostic disable-next-line: lowercase-global
vsr = {}

-- Scene creation ---------------------------------------------------------

---@return vsr.Scene
function vsr.createScene() end

-- ANARI data type constants ----------------------------------------------

---@type integer
vsr.GEOMETRY = 0
---@type integer
vsr.MATERIAL = 0
---@type integer
vsr.LIGHT = 0
---@type integer
vsr.CAMERA = 0
---@type integer
vsr.SURFACE = 0
---@type integer
vsr.VOLUME = 0
---@type integer
vsr.SAMPLER = 0
---@type integer
vsr.ARRAY = 0
---@type integer
vsr.SPATIAL_FIELD = 0

-- Math utility functions (MathBindings.cpp) ------------------------------

--- Construct a float2.
---@overload fun(): vsr.float2
---@overload fun(x: number, y: number): vsr.float2
---@return vsr.float2
function vsr.float2(...) end

--- Construct a float3.
---@overload fun(): vsr.float3
---@overload fun(x: number, y: number, z: number): vsr.float3
---@return vsr.float3
function vsr.float3(...) end

--- Construct a float4.
---@overload fun(): vsr.float4
---@overload fun(x: number, y: number, z: number, w: number): vsr.float4
---@return vsr.float4
function vsr.float4(...) end

--- Construct a mat3 (packed SRT: columns = scale, euler-rotation-degrees, translation).
---@overload fun(): vsr.mat3
---@overload fun(col0: vsr.float3, col1: vsr.float3, col2: vsr.float3): vsr.mat3
---@return vsr.mat3
function vsr.mat3(...) end

--- Construct a mat4.
---@overload fun(): vsr.mat4
---@overload fun(col0: vsr.float4, col1: vsr.float4, col2: vsr.float4, col3: vsr.float4): vsr.mat4
---@return vsr.mat4
function vsr.mat4(...) end

--- Alias for vsr.mat3 — construct a packed SRT matrix.
---@overload fun(): vsr.mat3
---@param scale vsr.float3
---@param rotation vsr.float3 # Euler rotation in degrees
---@param translation vsr.float3
---@return vsr.mat3
function vsr.srt(scale, rotation, translation) end

--- Compute the length of a vector.
---@overload fun(v: vsr.float2): number
---@overload fun(v: vsr.float3): number
---@overload fun(v: vsr.float4): number
---@return number
function vsr.length(v) end

--- Normalize a vector to unit length.
---@overload fun(v: vsr.float2): vsr.float2
---@overload fun(v: vsr.float3): vsr.float3
---@overload fun(v: vsr.float4): vsr.float4
function vsr.normalize(v) end

--- Compute the dot product of two vectors.
---@overload fun(a: vsr.float2, b: vsr.float2): number
---@overload fun(a: vsr.float3, b: vsr.float3): number
---@overload fun(a: vsr.float4, b: vsr.float4): number
---@return number
function vsr.dot(a, b) end

--- Compute the cross product of two float3 vectors.
---@param a vsr.float3
---@param b vsr.float3
---@return vsr.float3
function vsr.cross(a, b) end

--- Create a translation matrix.
---@param t vsr.float3
---@return vsr.mat4
function vsr.translation(t) end

--- Create a scaling matrix.
---@overload fun(s: vsr.float3): vsr.mat4
---@overload fun(s: number): vsr.mat4
---@return vsr.mat4
function vsr.scaling(s) end

--- Create a rotation matrix from an axis and angle (in radians).
---@param axis vsr.float3
---@param angle number
---@return vsr.mat4
function vsr.rotation(axis, angle) end

--- Convert degrees to radians.
---@param degrees number
---@return number
function vsr.radians(degrees) end

--- Convert radians to degrees.
---@param radians number
---@return number
function vsr.degrees(radians) end

------------------------------------------------------------------------
-- Sub-tables
------------------------------------------------------------------------

-- vsr.viewer --------------------------------------------------------------
-- NOTE: vsr.viewer is only available inside the interactive viewer (vsrViewer).
-- In standalone vsrLua, vsr.viewer is nil.  Guard with: if vsr.viewer then ... end

---@class vsr.viewer
vsr.viewer = {}

--- Request a viewer refresh (re-render the current frame).
function vsr.viewer.refresh() end

--- Register a menu action in the Lua menu.
--- The path uses `/` separators to define the menu hierarchy (e.g. "Import/glTF/Box").
---@param path string Menu path (e.g. "Category/Subcategory/Action Name")
---@param fn function The function to call when the action is selected
function vsr.viewer.addMenuAction(path, fn) end

--- Add a separator in the Lua menu under the given category path.
---@param categoryPath string Menu path for the separator (e.g. "Category/Subcategory")
function vsr.viewer.addSeparator(categoryPath) end

--- Clear all registered actions from the Lua menu.
function vsr.viewer.clearActions() end

-- vsr.io (IOBindings.cpp) ------------------------------------------------

---@class vsr.io
vsr.io = {}

--- Import an OBJ file.
---@overload fun(scene: vsr.Scene, filename: string)
---@overload fun(scene: vsr.Scene, filename: string, location: vsr.LayerNode)
---@overload fun(scene: vsr.Scene, filename: string, location: vsr.LayerNode, useDefaultMat: boolean)
function vsr.io.importOBJ(...) end

--- Import a glTF file.
---@overload fun(scene: vsr.Scene, filename: string)
---@overload fun(scene: vsr.Scene, filename: string, location: vsr.LayerNode)
function vsr.io.importGLTF(...) end

--- Import a PLY file.
---@overload fun(scene: vsr.Scene, filename: string)
---@overload fun(scene: vsr.Scene, filename: string, location: vsr.LayerNode)
function vsr.io.importPLY(...) end

--- Import an HDRI environment map.
---@overload fun(scene: vsr.Scene, filename: string)
---@overload fun(scene: vsr.Scene, filename: string, location: vsr.LayerNode)
function vsr.io.importHDRI(...) end

--- Import a USD file.
--
-- The settings table mirrors the import options:
--   purposes        = { default = true, render = true, proxy = false, guide = false }
--   renderContexts  = { "", "glslfx" }
--   materialMode    = "physicallyBased" | "materialx" | "mdl"
--   refinementLevel = 2
--   primPath        = "/World/Asset"
---@overload fun(scene: vsr.Scene, filename: string)
---@overload fun(scene: vsr.Scene, filename: string, location: vsr.LayerNode)
---@overload fun(scene: vsr.Scene, filename: string, location: vsr.LayerNode, settings: table)
function vsr.io.importUSD(...) end

--- Import a PBRT v4 scene file.
---@overload fun(scene: vsr.Scene, filename: string)
---@overload fun(scene: vsr.Scene, filename: string, location: vsr.LayerNode)
function vsr.io.importPBRT(...) end

--- Import a PDB (Protein Data Bank) file.
---@overload fun(scene: vsr.Scene, filename: string)
---@overload fun(scene: vsr.Scene, filename: string, location: vsr.LayerNode)
function vsr.io.importPDB(...) end

--- Import an SWC (neuron morphology) file.
---@overload fun(scene: vsr.Scene, filename: string)
---@overload fun(scene: vsr.Scene, filename: string, location: vsr.LayerNode)
function vsr.io.importSWC(...) end

--- Import an AGX file.
---@overload fun(scene: vsr.Scene, filename: string)
---@overload fun(scene: vsr.Scene, filename: string, location: vsr.LayerNode)
function vsr.io.importAGX(...) end

--- Import via ASSIMP (supports many formats).
---@overload fun(scene: vsr.Scene, filename: string)
---@overload fun(scene: vsr.Scene, filename: string, location: vsr.LayerNode)
---@overload fun(scene: vsr.Scene, filename: string, location: vsr.LayerNode, flatten: boolean)
function vsr.io.importASSIMP(...) end

--- Import an AXYZ point cloud file.
---@overload fun(scene: vsr.Scene, filename: string)
---@overload fun(scene: vsr.Scene, filename: string, location: vsr.LayerNode)
function vsr.io.importAXYZ(...) end

--- Import a DLAF file.
---@overload fun(scene: vsr.Scene, filename: string)
---@overload fun(scene: vsr.Scene, filename: string, location: vsr.LayerNode)
---@overload fun(scene: vsr.Scene, filename: string, location: vsr.LayerNode, useDefaultMat: boolean)
function vsr.io.importDLAF(...) end

--- Import an E57 point cloud file.
---@overload fun(scene: vsr.Scene, filename: string)
---@overload fun(scene: vsr.Scene, filename: string, location: vsr.LayerNode)
function vsr.io.importE57XYZ(...) end

--- Import an EnSight Gold case file.
--- Fields selects which variables to load (up to 4 ANARI attribute slots).
--- Timestep selects which time step index to load (0-based).
---@overload fun(scene: vsr.Scene, filename: string)
---@overload fun(scene: vsr.Scene, filename: string, location: vsr.LayerNode)
---@overload fun(scene: vsr.Scene, filename: string, location: vsr.LayerNode, fields: string[])
---@overload fun(scene: vsr.Scene, filename: string, location: vsr.LayerNode, fields: string[], timestep: integer)
function vsr.io.importENSIGHT(...) end

--- Import an HSMESH file.
---@overload fun(scene: vsr.Scene, filename: string)
---@overload fun(scene: vsr.Scene, filename: string, location: vsr.LayerNode)
function vsr.io.importHSMESH(...) end

--- Import an N-body simulation file.
---@overload fun(scene: vsr.Scene, filename: string)
---@overload fun(scene: vsr.Scene, filename: string, location: vsr.LayerNode)
---@overload fun(scene: vsr.Scene, filename: string, location: vsr.LayerNode, useDefaultMat: boolean)
function vsr.io.importNBODY(...) end

--- Import POINTSBIN files (multi-file).
---@overload fun(scene: vsr.Scene, filepaths: string[])
---@overload fun(scene: vsr.Scene, filepaths: string[], location: vsr.LayerNode)
function vsr.io.importPOINTSBIN(...) end

--- Import a PT file.
---@overload fun(scene: vsr.Scene, filename: string)
---@overload fun(scene: vsr.Scene, filename: string, location: vsr.LayerNode)
function vsr.io.importPT(...) end

--- Import a Silo file (scene-level). Requires a location parameter.
---@param scene vsr.Scene
---@param filename string
---@param location vsr.LayerNode
function vsr.io.importSilo(scene, filename, location) end

--- Import an SMESH file.
---@overload fun(scene: vsr.Scene, filename: string)
---@overload fun(scene: vsr.Scene, filename: string, location: vsr.LayerNode)
---@overload fun(scene: vsr.Scene, filename: string, location: vsr.LayerNode, isAnimation: boolean)
function vsr.io.importSMESH(...) end

--- Import a TRK track file.
---@overload fun(scene: vsr.Scene, filename: string)
---@overload fun(scene: vsr.Scene, filename: string, location: vsr.LayerNode)
function vsr.io.importTRK(...) end

--- Import an XYZDP point cloud file.
---@overload fun(scene: vsr.Scene, filename: string)
---@overload fun(scene: vsr.Scene, filename: string, location: vsr.LayerNode)
function vsr.io.importXYZDP(...) end

--- Import a volume file (auto-detects format).
---@overload fun(scene: vsr.Scene, filename: string): vsr.Volume
---@overload fun(scene: vsr.Scene, filename: string, location: vsr.LayerNode): vsr.Volume
function vsr.io.importVolume(...) end

--- Import a RAW volume file.
---@param scene vsr.Scene
---@param filename string
---@return vsr.SpatialField
function vsr.io.importRAW(scene, filename) end

--- Import a NanoVDB volume file.
---@param scene vsr.Scene
---@param filename string
---@return vsr.SpatialField
function vsr.io.importNVDB(scene, filename) end

--- Import an MHD (MetaImage) volume file.
---@param scene vsr.Scene
---@param filename string
---@return vsr.SpatialField
function vsr.io.importMHD(scene, filename) end

--- Import a FLASH (HDF5 AMR) volume file.
---@param scene vsr.Scene
---@param filename string
---@return vsr.SpatialField
function vsr.io.importFLASH(scene, filename) end

--- Import a VTI (VTK ImageData) volume file.
---@param scene vsr.Scene
---@param filename string
---@return vsr.SpatialField
function vsr.io.importVTI(scene, filename) end

--- Import a VTU (VTK UnstructuredGrid) volume file.
---@param scene vsr.Scene
---@param filename string
---@return vsr.SpatialField
function vsr.io.importVTU(scene, filename) end

--- Generate random spheres.
---@overload fun(scene: vsr.Scene)
---@overload fun(scene: vsr.Scene, location: vsr.LayerNode)
---@overload fun(scene: vsr.Scene, location: vsr.LayerNode, useDefaultMat: boolean)
function vsr.io.generateRandomSpheres(...) end

--- Generate a procedural icosphere (subdivided icosahedron), optionally on a
--- checkered floor.
---@overload fun(scene: vsr.Scene)
---@overload fun(scene: vsr.Scene, location: vsr.LayerNode)
---@overload fun(scene: vsr.Scene, location: vsr.LayerNode, subdivisions: integer)
---@overload fun(scene: vsr.Scene, location: vsr.LayerNode, subdivisions: integer, withFloor: boolean)
function vsr.io.generateIcosphere(...) end

--- Generate a Blender monkey (Suzanne).
---@overload fun(scene: vsr.Scene)
---@overload fun(scene: vsr.Scene, location: vsr.LayerNode)
function vsr.io.generateMonkey(...) end

--- Generate sample cylinders.
---@overload fun(scene: vsr.Scene)
---@overload fun(scene: vsr.Scene, location: vsr.LayerNode)
---@overload fun(scene: vsr.Scene, location: vsr.LayerNode, useDefaultMat: boolean)
function vsr.io.generateCylinders(...) end

--- Generate default scene lights.
---@param scene vsr.Scene
function vsr.io.generateDefaultLights(scene) end

--- Generate an HDRI dome light.
---@overload fun(scene: vsr.Scene)
---@overload fun(scene: vsr.Scene, location: vsr.LayerNode)
function vsr.io.generateHdriDome(...) end

--- Generate the "Ray Tracing in One Weekend" scene.
---@overload fun(scene: vsr.Scene)
---@overload fun(scene: vsr.Scene, location: vsr.LayerNode)
function vsr.io.generateRtow(...) end

--- Generate a sphere-set volume.
---@overload fun(scene: vsr.Scene)
---@overload fun(scene: vsr.Scene, location: vsr.LayerNode)
function vsr.io.generateSphereSetVolume(...) end

--- Create a default RGB-ramp color map array (float4, with alpha).
---@param scene vsr.Scene
---@param size? integer  Number of samples (default 256).
---@return vsr.Array
function vsr.io.makeDefaultColorMap(scene, size) end

--- Save a Scene Archive to a VSR file.
---@param scene vsr.Scene
---@param filename string
function vsr.io.saveSceneArchive(scene, filename) end

--- Load a Scene Archive from a VSR file.
---@param scene vsr.Scene
---@param filename string
function vsr.io.loadSceneArchive(scene, filename) end

--- Save an Animation Manager Archive to a VSR file.
---@param manager vsr.AnimationManager
---@param filename string
function vsr.io.saveAnimationManagerArchive(manager, filename) end

--- Load an Animation Manager Archive from a VSR file.
---@param manager vsr.AnimationManager
---@param filename string
function vsr.io.loadAnimationManagerArchive(manager, filename) end

-- vsr.render (RenderBindings.cpp) ----------------------------------------

---@class vsr.render
vsr.render = {}

--- Load an ANARI device by library name.
---@param libraryName string
---@return vsr.AnariDevice
function vsr.render.loadDevice(libraryName) end

--- Create a scene-owned render index for live scene updates.
--- Throws if `device` is nil or invalid.
---@param scene vsr.Scene
---@param device vsr.AnariDevice
---@return vsr.RenderIndex
function vsr.render.createRenderIndex(scene, device) end

--- Get the world bounds from a render index.
--- Throws if `device` or `index` is nil or invalid.
---@param device vsr.AnariDevice
---@param index vsr.RenderIndex
---@return {min: vsr.float3, max: vsr.float3}
function vsr.render.getWorldBounds(device, index) end

--- Create a render pipeline with a scene render pass.
--- Throws if width/height are <= 0, or if `device`/`index` are nil/invalid.
---@param width integer
---@param height integer
---@param device vsr.AnariDevice
---@param index vsr.RenderIndex
---@param camera vsr.CameraSetup
--- Optional renderer parameters.
--- Special key "renderer" selects subtype (default: "default").
--- Supports vector values for params like background (float4), ambientColor (float3).
---@param rendererParams? table<string, boolean|number|string|vsr.float2|vsr.float3|vsr.float4|vsr.mat4|number[]>
---@return vsr.ImagePipeline
function vsr.render.createPipeline(width, height, device, index, camera, rendererParams) end

--- Render multiple samples and save to an image file.
--- Supported formats: png, jpg/jpeg, bmp, tga, ppm.
--- Throws if `pipeline` is nil, `samples < 1`, or width/height are <= 0.
--- The pipeline dimensions are set to `(width, height)` before rendering.
---@param pipeline vsr.ImagePipeline
---@param samples integer
---@param filename string
---@param width integer
---@param height integer
function vsr.render.renderToFile(pipeline, samples, filename, width, height) end

------------------------------------------------------------------------
-- Global variable: the pre-bound scene instance
------------------------------------------------------------------------

---@type vsr.Scene
scene = nil

---@type vsr.AnimationManager
animationMgr = nil

return vsr
