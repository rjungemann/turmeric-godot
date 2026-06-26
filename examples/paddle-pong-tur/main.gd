extends Node2D

# Live paddle-pong runner. Mirrors the wiring in pong_driver.gd (which is a
# headless SceneTree test) but builds the scene under a Node2D so it can be
# launched from the editor with F5. Adds Polygon2D children so the otherwise
# invisible Node2D ball and paddles actually render.

const BALL_HALF := 6.0
const PADDLE_HALF_W := 6.0
const PADDLE_HALF_H := 40.0

func _make_box(half_w: float, half_h: float, color: Color) -> Polygon2D:
	var poly := Polygon2D.new()
	poly.polygon = PackedVector2Array([
		Vector2(-half_w, -half_h),
		Vector2( half_w, -half_h),
		Vector2( half_w,  half_h),
		Vector2(-half_w,  half_h),
	])
	poly.color = color
	return poly

func _ready() -> void:
	var ball_script := load("res://scripts/ball.tur")
	var paddle_script := load("res://scripts/paddle.tur")
	var score_script := load("res://scripts/score.tur")
	if ball_script == null or paddle_script == null or score_script == null:
		push_error("[pong] script load returned null")
		return

	var ball := Node2D.new()
	ball.name = "Ball"
	ball.set_script(ball_script)
	ball.position = Vector2(100, 240)
	ball.set("vel-x", 120.0)
	ball.set("vel-y", 240.0)
	ball.set("bound-y", 480.0)
	ball.add_child(_make_box(BALL_HALF, BALL_HALF, Color(1, 1, 1)))
	add_child(ball)

	var p1 := Node2D.new()
	p1.name = "P1"
	p1.set_script(paddle_script)
	p1.position = Vector2(20, 240)
	p1.set("vy", -200.0)
	p1.add_child(_make_box(PADDLE_HALF_W, PADDLE_HALF_H, Color(0.6, 0.9, 1.0)))
	add_child(p1)

	var p2 := Node2D.new()
	p2.name = "P2"
	p2.set_script(paddle_script)
	p2.position = Vector2(620, 240)
	p2.set("vy", 200.0)
	p2.add_child(_make_box(PADDLE_HALF_W, PADDLE_HALF_H, Color(1.0, 0.7, 0.6)))
	add_child(p2)

	var label := Label.new()
	label.name = "Score"
	label.set_script(score_script)
	label.set("midpoint", 320.0)
	label.text = "init"
	label.position = Vector2(280, 20)
	add_child(label)
