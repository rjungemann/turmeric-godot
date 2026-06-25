@tool
extends SceneTree

# G5 paddle-pong-tur driver. The plan's headline G5 deliverable -- a
# real gameplay loop driven entirely by Turmeric scripts, with the
# GDScript driver here only to set up the scene and assert post-frame
# state.
#
# What this exercises:
#   * .tur scripts attached via set_script + add_child
#   * (godot-export "name" "type" default) round-trip through the
#     inspector storage (godot-prop-set/get touched from script)
#   * _process(delta) lifecycle dispatch under varying delta
#   * The ball's bounce logic: vel-y flips when the y position
#     would cross a wall

var fail_count := 0

func _fail(msg: String) -> void:
	fail_count += 1
	printerr("[pong] FAIL: ", msg)

func _approx(a: float, b: float, eps: float = 0.01) -> bool:
	return abs(a - b) <= eps

func _init() -> void:
	var ball_script := load("res://scripts/ball.tur")
	var paddle_script := load("res://scripts/paddle.tur")
	var score_script := load("res://scripts/score.tur")
	if ball_script == null or paddle_script == null or score_script == null:
		_fail("script load returned null"); quit(); return

	# Ball: position (100, 240); pre-set vel-x=120, vel-y=240 (moves
	# down-right). Bound-y = 480; ball must hit bottom in ~one second
	# and flip vel-y.
	var ball := Node2D.new()
	ball.name = "Ball"
	ball.set_script(ball_script)
	ball.position = Vector2(100, 240)
	ball.set("vel-x",   120.0)
	ball.set("vel-y",   240.0)
	ball.set("bound-y", 480.0)
	root.add_child(ball)

	# Two paddles: left + right. Driver pushes paddle1.vy = -200 (up)
	# and paddle2.vy = 200 (down) to simulate held input.
	var p1 := Node2D.new()
	p1.name = "P1"; p1.set_script(paddle_script); p1.position = Vector2(20, 240)
	p1.set("vy", -200.0)
	root.add_child(p1)

	var p2 := Node2D.new()
	p2.name = "P2"; p2.set_script(paddle_script); p2.position = Vector2(620, 240)
	p2.set("vy", 200.0)
	root.add_child(p2)

	# Label sibling of Ball -- score.tur reads ../Ball each frame.
	var label := Label.new()
	label.name = "Score"
	label.set_script(score_script)
	label.set("midpoint", 320.0)
	label.text = "init"
	root.add_child(label)

	# Let one frame fire _ready + register exports; subsequent frames
	# drive _process.
	await process_frame
	# 200 frames * ~16ms each = ~3.2s simulated. Plenty for the ball
	# (vel-y=240, bound=480, starts at y=240) to overshoot the bottom
	# wall and trigger the bounce.
	for _i in range(200):
		await process_frame

	# --- Assertions ---------------------------------------------------------

	# Ball x advanced monotonically right (vel-x=120 px/sec * ~0.5s
	# = ~60px). Use a loose lower bound; headless dt is variable.
	if ball.position.x <= 100.5:
		_fail("ball did not move right; pos=%s" % str(ball.position))

	# Ball y is in [0, 480] -- never escaped the bounds clamp.
	if ball.position.y < 0.0 or ball.position.y > 480.0:
		_fail("ball escaped y bounds; pos.y=%s" % str(ball.position.y))

	# Ball vel-y flipped sign at least once (started 240, must be
	# negative after bouncing off bottom).
	var vy_now := float(ball.get("vel-y"))
	if vy_now >= 0.0:
		_fail("ball vel-y did not flip; vy=%s" % str(vy_now))

	# Paddles moved under their simulated vy.
	if p1.position.y >= 240.0:
		_fail("P1 did not move up; y=%s" % str(p1.position.y))
	if p2.position.y <= 240.0:
		_fail("P2 did not move down; y=%s" % str(p2.position.y))

	# Label was updated by score.tur via (label/set-text (godot-num->str bx)).
	# The text is the live ball x as a number; assert it parses as a
	# float roughly matching ball.position.x (within one frame's drift,
	# since the label updates *before* the final ball move of the run).
	var parsed := float(label.text)
	if abs(parsed - ball.position.x) > 5.0:
		_fail("Label text %s doesn't match ball.x=%s" % [str(label.text), str(ball.position.x)])

	if fail_count == 0:
		print("[pong] all assertions passed: ball=%s vy=%.2f  p1.y=%.2f  p2.y=%.2f" %
			[str(ball.position), vy_now, p1.position.y, p2.position.y])
	else:
		printerr("[pong] %d FAILURES" % fail_count)

	for n in [ball, p1, p2, label]:
		n.queue_free()
	quit(0 if fail_count == 0 else 1)
