import taichi as ti
import taichi_glsl as ts
ti.init(arch=ti.vulkan)

dt = 1.0 / 600
R = 0.1
G = 0.8

Ks = 300
Eta = 30  # 1
Kt = 1

N = 33
x0 = ti.Vector.field(3, float, (N,))
x = ti.Vector.field(3, float, (N,))
v = ti.Vector.field(3, float, (N,))

M = 11
bodyIdx = ti.Vector.field(2, int, (M,))   # (start, end)
bodyPos = ti.Vector.field(3, float, (M,))
bodyVel = ti.Vector.field(3, float, (M,))
bodyIne = ti.Matrix.field(3, 3, float, (M,))
bodyAcc = ti.Vector.field(3, float, (M,))
bodyAng = ti.Vector.field(3, float, (M,))
bodyOri = ti.Vector.field(4, float, (M,)) # quaternion, (x, y, z, w)

count = ti.field(int, ())

@ti.kernel
def init():
  for i in range(M):
    x0[i * 3 + 0] = ti.Vector([R * 0.1, -R * 0.9, 0])
    x0[i * 3 + 1] = ti.Vector([0, 0, 0])
    x0[i * 3 + 2] = ti.Vector([-R * 0.1, R * 0.9, 0])
    bodyPos[i] = ti.Vector([-0.5 + R * 0.9 * i, R * 0.2 * i + R, 0])
    bodyIdx[i] = ti.Vector([i * 3, i * 3 + 3])
    bodyVel[i] = ti.Vector([-0.2, ti.random() * 0.5, 0])
    bodyAcc[i] = ti.Vector([0, 0, 0])
    bodyAng[i] = ti.Vector([0, 0, 0])
    bodyOri[i] = ti.Vector([0, 0, 0, 1])
    # Inverse inertia
    ine = ti.Matrix([[0.0, 0.0, 0.0], [0.0, 0.0, 0.0], [0.0, 0.0, 0.0]])
    for j in range(i * 3, i * 3 + 3):
      m = 5
      for p, q in ti.static(ti.ndrange(3, 3)):
        ine[p, q] -= m * x0[j][p] * x0[j][q]
        if p == q:
          ine[p, q] += m * x0[j].norm() ** 2
    bodyIne[i] = ine.inverse()

@ti.func
def quat_mul(a, b):
  av = a.xyz
  bv = b.xyz
  rv = a.w * bv + b.w * av + av.cross(bv)
  w = a.w * b.w - av.dot(bv)
  return ti.Vector([rv.x, rv.y, rv.z, w])

@ti.func
def quat_rot(v, q):
  return quat_mul(quat_mul(
    q,
    ti.Vector([ v.x,  v.y,  v.z, 0])),
    ti.Vector([-q.x, -q.y, -q.z, q.w])
  ).xyz

@ti.func
def quat_mat(q):
  x, y, z, w = q.x, q.y, q.z, q.w
  return ti.Matrix([
    [1 - 2*y*y - 2*z*z, 2*x*y - 2*w*z, 2*x*z + 2*w*y],
    [2*x*y + 2*w*z, 1 - 2*x*x - 2*z*z, 2*y*z - 2*w*x],
    [2*x*z - 2*w*y, 2*y*z + 2*w*x, 1 - 2*x*x - 2*y*y]
  ])

@ti.func
def step():
  # Verlet integration pre-step
  for i in range(M):
    bodyPos[i] += bodyVel[i] * dt + bodyAcc[i] * (0.5*dt*dt)

  # Calculate particle position and velocity
  for i in range(M):
    for j in range(bodyIdx[i][0], bodyIdx[i][1]):
      r = quat_rot(x0[j], bodyOri[i])
      x[j] = bodyPos[i] + r
      v[j] = bodyVel[i] + bodyAng[i].cross(r)

  # Collision
  for b in range(M):
    fSum = ti.Vector([0.0, 0.0, 0.0])
    tSum = ti.Vector([0.0, 0.0, 0.0])
    boundaryX = 0.0
    boundaryY = 0.0
    for i in range(bodyIdx[b][0], bodyIdx[b][1]):
      f = ti.Vector([0.0, 0.0, 0.0])
      for j in range(N):
        if j >= bodyIdx[b][0] and j < bodyIdx[b][1]: continue
        d = (x[i] - x[j]).norm()
        if d < R:
          # Repulsive
          dirUnit = (x[i] - x[j]).normalized()
          f += Ks * (R * 2 - d) * dirUnit
          # Damping
          relVel = v[j] - v[i]
          f += Eta * relVel
          # Shear
          f += Kt * (relVel - (relVel.dot(dirUnit) * dirUnit))
      # Impulse from boundaries
      if (abs(v[i].y) > boundaryY and
          x[i].y < -0.5 + R and v[i].y < 0):
        boundaryY = v[i].y
      if (abs(v[i].x) > boundaryX and
          (x[i].x < -0.8 + R and v[i].x < 0) or
          (x[i].x >  0.8 - R and v[i].x > 0)):
        boundaryX = v[i].x
      fSum += f
      tSum += (x[i] - bodyPos[b]).cross(f)

    fSum.x += -boundaryX / (0.2 * dt) * 1.5 # 2
    fSum.y += -boundaryY / (0.2 * dt) * 1.5 # 2

    # Translational
    # Verlet integration post-step
    newAcc = fSum * 0.2
    newAcc.y -= G
    bodyVel[b] += (bodyAcc[b] + newAcc) * 0.5 * dt
    bodyAcc[b] = newAcc
    # Rotational
    bodyAng[b] += tSum * dt
    rotMat = quat_mat(bodyOri[b])
    angVel = (rotMat * bodyIne[b] * rotMat.transpose()).__matmul__(bodyAng[b])
    if bodyAng[b].norm() >= 1e-5:
      theta = bodyAng[b].norm() * dt
      dqw = ti.cos(theta / 2)
      dqv = ti.sin(theta / 2) * bodyAng[b].normalized()
      dq = ti.Vector([dqv.x, dqv.y, dqv.z, dqw])
      bodyOri[b] = quat_mul(dq, bodyOri[b])

@ti.kernel
def frame():
  for i in ti.static(range(10)):
    step()

init()

window = ti.ui.Window('Collision', (600, 600), vsync=True)
canvas = window.get_canvas()
scene = ti.ui.Scene()
camera = ti.ui.make_camera()

while window.running:
  frame()

  camera.position(0, 0, 2)
  camera.lookat(0, 0, 0)
  scene.set_camera(camera)

  scene.point_light(pos=(0.5, 1, 2), color=(0.5, 0.5, 0.5))
  scene.ambient_light(color=(0.5, 0.5, 0.5))
  scene.particles(x, radius=R, color=(0.6, 0.7, 1))
  canvas.scene(scene)
  window.show()