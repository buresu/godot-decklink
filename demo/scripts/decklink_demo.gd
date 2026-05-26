extends Control

const OUTPUT_FPS := 30.0

var _decklink
var _input
var _output
var _preview_texture: ImageTexture
var _output_pattern_texture: ImageTexture
var _output_time := 0.0
var _output_frame := 0

var _device_select: OptionButton
var _input_mode_select: OptionButton
var _output_mode_select: OptionButton
var _refresh_button: Button
var _input_button: Button
var _output_button: Button
var _send_once_button: Button
var _status_label: Label
var _preview: TextureRect
var _device_info: TextEdit

func _ready() -> void:
	_input = get_node_or_null("DeckLinkInput")
	_output = get_node_or_null("DeckLinkOutput")
	_build_ui()
	if Engine.has_singleton("DeckLink"):
		_decklink = Engine.get_singleton("DeckLink")
	_refresh_devices()

func _exit_tree() -> void:
	_stop_input()
	_stop_output()

func _process(delta: float) -> void:
	if _input != null and _input.is_open() and _input.has_frame():
		var image = _input.get_image()
		if image != null:
			if _preview_texture == null:
				_preview_texture = ImageTexture.create_from_image(image)
				_preview.texture = _preview_texture
			else:
				_preview_texture.update(image)

	if _output != null and _output.is_sending():
		_output_time += delta
		var interval := 1.0 / OUTPUT_FPS
		while _output_time >= interval:
			_output_time -= interval
			_update_output_pattern_texture()

func _build_ui() -> void:
	var root := VBoxContainer.new()
	root.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.offset_left = 16
	root.offset_top = 16
	root.offset_right = -16
	root.offset_bottom = -16
	root.add_theme_constant_override("separation", 10)
	add_child(root)

	var top_row := HBoxContainer.new()
	top_row.add_theme_constant_override("separation", 8)
	root.add_child(top_row)

	_device_select = OptionButton.new()
	_device_select.custom_minimum_size = Vector2(320, 0)
	_device_select.item_selected.connect(_on_device_selected)
	top_row.add_child(_device_select)

	_refresh_button = Button.new()
	_refresh_button.text = "Refresh"
	_refresh_button.pressed.connect(_refresh_devices)
	top_row.add_child(_refresh_button)

	var mode_row := HBoxContainer.new()
	mode_row.add_theme_constant_override("separation", 8)
	root.add_child(mode_row)

	_input_mode_select = OptionButton.new()
	_input_mode_select.custom_minimum_size = Vector2(360, 0)
	mode_row.add_child(_input_mode_select)

	_output_mode_select = OptionButton.new()
	_output_mode_select.custom_minimum_size = Vector2(360, 0)
	mode_row.add_child(_output_mode_select)

	var action_row := HBoxContainer.new()
	action_row.add_theme_constant_override("separation", 8)
	root.add_child(action_row)

	_input_button = Button.new()
	_input_button.text = "Start Input"
	_input_button.pressed.connect(_toggle_input)
	action_row.add_child(_input_button)

	_output_button = Button.new()
	_output_button.text = "Start Output Pattern"
	_output_button.pressed.connect(_toggle_output)
	action_row.add_child(_output_button)

	_send_once_button = Button.new()
	_send_once_button.text = "Send One Frame"
	_send_once_button.pressed.connect(_send_one_frame)
	action_row.add_child(_send_once_button)

	_status_label = Label.new()
	_status_label.text = "Ready"
	root.add_child(_status_label)

	var body := HSplitContainer.new()
	body.size_flags_vertical = Control.SIZE_EXPAND_FILL
	root.add_child(body)

	_preview = TextureRect.new()
	_preview.expand_mode = TextureRect.EXPAND_FIT_WIDTH_PROPORTIONAL
	_preview.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	_preview.custom_minimum_size = Vector2(720, 405)
	body.add_child(_preview)

	_device_info = TextEdit.new()
	_device_info.editable = false
	_device_info.wrap_mode = TextEdit.LINE_WRAPPING_BOUNDARY
	_device_info.custom_minimum_size = Vector2(360, 0)
	body.add_child(_device_info)

func _refresh_devices() -> void:
	_stop_input()
	_stop_output()
	_preview.texture = null
	_preview_texture = null
	_output_pattern_texture = null

	_device_select.clear()
	_device_info.text = ""

	if _decklink == null:
		_set_controls_enabled(false)
		_status_label.text = "DeckLink extension is not loaded"
		return

	_decklink.refresh()

	var devices: Array = _decklink.get_devices()
	for device in devices:
		var label := "%d: %s" % [device.get("index", 0), _device_label(device)]
		_device_select.add_item(label)
		_device_select.set_item_metadata(_device_select.item_count - 1, device.get("index", 0))

	_set_controls_enabled(not devices.is_empty())

	if devices.is_empty():
		_status_label.text = "No DeckLink devices found"
		_input_mode_select.clear()
		_output_mode_select.clear()
		return

	_device_select.select(0)
	_load_modes_for_selected_device()
	_status_label.text = "Found %d DeckLink device(s)" % devices.size()

func _on_device_selected(_index: int) -> void:
	_stop_input()
	_stop_output()
	_load_modes_for_selected_device()

func _load_modes_for_selected_device() -> void:
	var device_index := _selected_device_index()
	if device_index < 0:
		return

	_fill_mode_select(_input_mode_select, _decklink.get_input_display_modes(device_index))
	_fill_mode_select(_output_mode_select, _decklink.get_output_display_modes(device_index))

	var device_lines: Array[String] = []
	for device in _decklink.get_devices():
		if int(device.get("index", -1)) == device_index:
			device_lines.append("Device: %s" % _device_label(device))
			device_lines.append("")
			break
	device_lines.append("Input modes: %d" % _input_mode_select.item_count)
	device_lines.append("Output modes: %d" % _output_mode_select.item_count)
	_device_info.text = "\n".join(device_lines)

func _fill_mode_select(select: OptionButton, modes: Array) -> void:
	select.clear()
	for mode in modes:
		select.add_item(_mode_label(mode))
		select.set_item_metadata(select.item_count - 1, int(mode.get("id", 0)))
	select.disabled = modes.is_empty()
	if not modes.is_empty():
		select.select(0)

func _toggle_input() -> void:
	if _input != null and _input.is_receiving():
		_stop_input()
		return
	if _output != null and _output.is_sending():
		_stop_output()

	var device_index := _selected_device_index()
	var mode := _selected_mode(_input_mode_select)
	if device_index < 0 or mode == 0:
		_status_label.text = "Select an input device and mode"
		return

	if _input == null:
		_status_label.text = "DeckLinkInput class is not available"
		return
	_input.device_index = device_index
	_input.display_mode = mode
	_input.receiving = true
	if _input.is_open():
		_input_button.text = "Stop Input"
		_status_label.text = "Input started"
	else:
		_status_label.text = "Input failed"

func _toggle_output() -> void:
	if _output != null and _output.is_sending():
		_stop_output()
		return
	if _input != null and _input.is_receiving():
		_stop_input()

	var device_index := _selected_device_index()
	var mode := _selected_mode(_output_mode_select)
	if device_index < 0 or mode == 0:
		_status_label.text = "Select an output device and mode"
		return

	if _output == null:
		_status_label.text = "DeckLinkOutput class is not available"
		return
	_output.device_index = device_index
	_output.display_mode = mode
	_output.sending = true
	if _output.is_open():
		_output_button.text = "Stop Output Pattern"
		_output_time = 0.0
		_output_frame = 0
		_output_pattern_texture = null
		_update_output_pattern_texture()
		_status_label.text = "Output pattern started"
	else:
		_status_label.text = "Output failed"

func _send_one_frame() -> void:
	if _input != null and _input.is_receiving():
		_stop_input()

	if _output == null or not _output.is_open():
		var device_index := _selected_device_index()
		var mode := _selected_mode(_output_mode_select)
		if device_index < 0 or mode == 0:
			_status_label.text = "Select an output device and mode"
			return
		if _output == null:
			_status_label.text = "DeckLinkOutput class is not available"
			return
		_output.device_index = device_index
		_output.display_mode = mode
		if not _output.open(device_index, mode):
			_status_label.text = "Output failed"
			return
		_output_button.text = "Stop Output Pattern"

	_send_output_frame()
	_status_label.text = "Sent one output frame"

func _send_output_frame() -> void:
	if _output == null or not _output.is_open():
		return

	var width: int = _output.get_width()
	var height: int = _output.get_height()
	if width <= 0 or height <= 0:
		return

	var data := PackedByteArray()
	data.resize(width * height * 4)
	var stripe_width: int = max(1, width / 8)
	var offset: int = _output_frame % max(1, width)

	for y in height:
		for x in width:
			var band := int((x + offset) / stripe_width) % 8
			var base := (y * width + x) * 4
			_write_pattern_rgba(data, base, band, x, y)
			data[base + 3] = 255

	var image := Image.create_from_data(width, height, false, Image.FORMAT_RGBA8, data)
	if not _output.output_image(image):
		_status_label.text = "Output frame failed"
	_output_frame += 3

func _update_output_pattern_texture() -> void:
	if _output == null or not _output.is_open():
		return

	var width: int = _output.get_width()
	var height: int = _output.get_height()
	if width <= 0 or height <= 0:
		return

	var image := _create_pattern_image(width, height)
	if _output_pattern_texture == null or _output_pattern_texture.get_width() != width or _output_pattern_texture.get_height() != height:
		_output_pattern_texture = ImageTexture.create_from_image(image)
		_output.set_texture(_output_pattern_texture)
	else:
		_output_pattern_texture.update(image)
	_output_frame += 3

func _create_pattern_image(width: int, height: int) -> Image:
	var data := PackedByteArray()
	data.resize(width * height * 4)
	var stripe_width: int = max(1, width / 8)
	var offset: int = _output_frame % max(1, width)

	for y in height:
		for x in width:
			var band := int((x + offset) / stripe_width) % 8
			var base := (y * width + x) * 4
			_write_pattern_rgba(data, base, band, x, y)
			data[base + 3] = 255

	return Image.create_from_data(width, height, false, Image.FORMAT_RGBA8, data)

func _stop_input() -> void:
	if _input != null:
		_input.receiving = false
	if _input_button != null:
		_input_button.text = "Start Input"

func _stop_output() -> void:
	if _output != null:
		_output.sending = false
		_output.set_texture(null)
	_output_pattern_texture = null
	if _output_button != null:
		_output_button.text = "Start Output Pattern"

func _set_controls_enabled(enabled: bool) -> void:
	_device_select.disabled = not enabled
	_input_button.disabled = not enabled
	_output_button.disabled = not enabled
	_send_once_button.disabled = not enabled

func _selected_device_index() -> int:
	var selected := _device_select.selected
	if selected < 0:
		return -1
	return int(_device_select.get_item_metadata(selected))

func _selected_mode(select: OptionButton) -> int:
	var selected := select.selected
	if selected < 0:
		return 0
	return int(select.get_item_metadata(selected))

func _device_label(device: Dictionary) -> String:
	var display_name := str(device.get("display_name", ""))
	if not display_name.is_empty():
		return display_name
	return str(device.get("model_name", "DeckLink"))

func _mode_label(mode: Dictionary) -> String:
	return "%s  %dx%d  %.2f fps" % [
		str(mode.get("name", "")),
		int(mode.get("width", 0)),
		int(mode.get("height", 0)),
		float(mode.get("fps", 0.0)),
	]

func _write_pattern_rgba(data: PackedByteArray, base: int, band: int, x: int, y: int) -> void:
	var r := 0
	var g := 0
	var b := 0
	match band:
		0:
			r = 255
			g = 255
			b = 255
		1:
			r = 255
			g = 255
		2:
			g = 255
			b = 255
		3:
			g = 255
		4:
			r = 255
			b = 255
		5:
			r = 255
		6:
			b = 255

	var marker := ((x / 32) + (y / 32) + (_output_frame / 12)) % 2
	if marker != 0:
		r = int(r * 0.75)
		g = int(g * 0.75)
		b = int(b * 0.75)

	data[base + 0] = r
	data[base + 1] = g
	data[base + 2] = b
