@tool
extends SceneTree

# G4 -- exercise TurmericSyntaxHighlighter.highlight_line_for_test on
# representative .tur lines and verify the returned {col -> {color}}
# dict has entries at the right columns and the right colors for the
# token kinds (keyword / comment / string / number / paren).

var fail_count := 0

func _fail(msg: String) -> void:
	fail_count += 1
	printerr("[highlighter.gd] FAIL: ", msg)

func _has_run_at(d: Dictionary, col: int) -> bool:
	return d.has(col)

func _color_at(d: Dictionary, col: int) -> Color:
	return (d[col] as Dictionary).get("color", Color())

func _approx_color(a: Color, b: Color) -> bool:
	return abs(a.r - b.r) < 0.01 and abs(a.g - b.g) < 0.01 and abs(a.b - b.b) < 0.01

# Expected colors must match those in TurmericSyntaxHighlighter::ctor.
const KEYWORD := Color(0.55, 0.70, 1.00)
const COMMENT := Color(0.45, 0.55, 0.45)
const STRING  := Color(0.85, 0.65, 0.45)
const NUMBER  := Color(0.70, 0.85, 0.55)
const PAREN   := Color(0.65, 0.65, 0.65)

func _init() -> void:
	var hl: Object = ClassDB.instantiate("TurmericSyntaxHighlighter")
	if hl == null:
		_fail("TurmericSyntaxHighlighter not registered"); quit(); return

	# Line 1 -- keyword + symbol + paren.
	# Cols:   0='(' 1='defn' 5=' ' 6='greet'
	var d1: Dictionary = hl.call("highlight_line_for_test", "(defn greet [] 42)")
	if not _has_run_at(d1, 0) or not _approx_color(_color_at(d1, 0), PAREN):
		_fail("expected paren color at col 0 of line1")
	if not _has_run_at(d1, 1) or not _approx_color(_color_at(d1, 1), KEYWORD):
		_fail("expected keyword color at col 1 ('defn')")

	# Line 2 -- comment whole line.
	var d2: Dictionary = hl.call("highlight_line_for_test", "  ;; a comment")
	# Column 2 is where ';' starts.
	if not _has_run_at(d2, 2) or not _approx_color(_color_at(d2, 2), COMMENT):
		_fail("expected comment color at col 2")

	# Line 3 -- string literal.
	var d3: Dictionary = hl.call("highlight_line_for_test", "(println \"hello\")")
	# '"' starts at col 9.
	if not _has_run_at(d3, 9) or not _approx_color(_color_at(d3, 9), STRING):
		_fail("expected string color at col 9: %s" % str(d3))

	# Line 4 -- number.
	var d4: Dictionary = hl.call("highlight_line_for_test", "(+ 1 2.5)")
	if not _has_run_at(d4, 3) or not _approx_color(_color_at(d4, 3), NUMBER):
		_fail("expected number color at col 3 ('1')")
	if not _has_run_at(d4, 5) or not _approx_color(_color_at(d4, 5), NUMBER):
		_fail("expected number color at col 5 ('2.5')")

	if fail_count == 0:
		print("[highlighter.gd] all assertions passed; sample line1=%s" % str(d1))
	else:
		printerr("[highlighter.gd] %d FAILURES" % fail_count)

	quit(0 if fail_count == 0 else 1)
