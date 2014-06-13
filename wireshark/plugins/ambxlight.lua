-- ########################################################
-- Cyborg amBX Light Pods USB dissector 
-- ########################################################
--
-- This dissector only supports Cyborg amBX Light Pods.
--
--   Author : Yuki Mizuno
--   Version: 0.0.1
--   License: GPLv3
--
-- ########################################################

ambxlight_proto = Proto("ambxlight", "Cyborg amBX Light Pods payload")


-- ========================================================
-- Cyborg amBX Light Pods USB payload fields definition.
-- ========================================================
status_F = ProtoField.string("ambxlight.status", "Status")
color_F = ProtoField.string("ambxlight.color", "Color")
color_hex_F = ProtoField.uint24("ambxlight.color.hex", "Hex", base.HEX)
color_rgb_F = ProtoField.string("ambxlight.color.rgb", "RGB")
speed_F = ProtoField.uint16("ambxlight.speed", "Speed")

-- ========================================================
-- Enable Cyborg amBX Light Pods USB payload fields.
-- ========================================================
ambxlight_proto.fields = {color_F, speed_F, color_hex_F, color_rgb_F}

-- ========================================================
-- Parse Cyborg amBX Light Pods USB payload fields.
-- ========================================================
function ambxlight_proto.dissector(buffer, pinfo, tree)

	local start = 30

	local ambxlight_range = buffer(start, 5)
	local color_range = buffer(start, 3)
	local speed_range = buffer(start + 3, 2)

	local color = color_range:uint()
	local speed = speed_range:uint()
	local color_name

	local subtree = tree:add(ambxlight_proto, ambxlight_range, "Cyborg amBX Light Pods USB payload Data")
	if color == 0x000000 then
		color_name = "Black"
	elseif color == 0xff0000 then
		color_name = "Red"
	elseif color == 0x00ff00 then
		color_name = "Green"
	elseif color == 0x0000ff then
		color_name = "Blue"
	elseif color == 0xffffff then
		color_name = "White"
	else
		color_name = "Unknown"
	end

	local subcolortree = subtree:add(color_F, buffer(start, 3), color_name)
	subcolortree:add(color_hex_F, color_range, color)
	subcolortree:add(color_rgb_F, color_range, string.format("rgb( %d, %d, %d)", (color / (256 * 256)), (color / 256) % 256, color % 256))
	subtree:add(speed_F, speed_range, (speed % 256) * 256 + (speed / 256))

	local data_dissector = Dissector.get("data")
	data_dissector:call(buffer(start + 5):tvb(), pinfo, tree)
end

-- ========================================================
-- Register ambxlight_proto.
-- ========================================================
register_postdissector(ambxlight_proto)
