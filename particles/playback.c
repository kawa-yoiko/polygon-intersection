#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "playback_utils.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define N 888

typedef struct particle_header {
  float radius;
  float mass;
  float elas;
  float body;
} particle_header;

typedef struct particle {
  float force[5][3];
  float pos[3];
  float vel[3];
  float contact[3];
} particle;

void MyDrawSphereWires(Vector3 centerPos, float radius, int rings, int slices, Color color);

Font font;
Font fontlarge;
void MyDrawText(const char *text, int posX, int posY, int fontSize, Color color)
{
  DrawTextEx(font, text, (Vector2){posX, posY}, fontSize, 0, color);
}
void MyDrawTextLarge(const char *text, int posX, int posY, int fontSize, Color color)
{
  DrawTextEx(fontlarge, text, (Vector2){posX, posY}, fontSize, 0, color);
}

#define norm3d(_v) \
  (sqrtf(((_v)[0])*((_v)[0]) + ((_v)[1])*((_v)[1]) + ((_v)[2])*((_v)[2])))

int main(int argc, char *argv[])
{
  int framesize = sizeof(particle) * N;
  int headersize = sizeof(particle_header) * N;

  const char *path = "record.bin";
  if (argc > 1) path = argv[1];

  FILE *f = fopen(path, "rb");
  fseek(f, 0, SEEK_END);
  int nframes = (ftell(f) - headersize) / framesize;
  fseek(f, 0, SEEK_SET);
  printf("%d particles, %d frames\n", N, nframes);

  particle_header *phs = (particle_header *)malloc(sizeof(particle_header) * N);
  fread(phs, sizeof(particle_header) * N, 1, f);
  particle *ps = (particle *)malloc(nframes * sizeof(particle) * N);
  fread(ps, nframes * sizeof(particle) * N, 1, f);

  Color *bodycolour = (Color *)malloc(sizeof(Color) * N);
  for (int i = 0; i < N; i++) {
    int r = rand() % 64;
    int g = rand() % 64;
    int b = rand() % 64;
    int base = 160 + (64 - (r * 2 + g * 5 + b) / 8) / 2;
    bodycolour[i] = (Color){r + base, g + base, b + base, 255};
  }

  int titlemaxlen = 16 + strlen(path);
  char *title = (char *)malloc(titlemaxlen);
  snprintf(title, titlemaxlen, "Playback [%s]", path);
  InitWindow(1280, 720, title);
  SetTargetFPS(60);

  font = LoadFontEx(
    "Brass_Mono_regular.otf", 32, 0, 0
  );
  fontlarge = LoadFontEx(
    "Brass_Mono_regular.otf", 48, 0, 0
  );

  Camera3D camera = (Camera3D){
    (Vector3){4, 5, 6},
    (Vector3){0, 0, 0},
    (Vector3){0, 1, 0},
    30,
    CAMERA_PERSPECTIVE
  };

  int frame = 0;
  int forcemask = 0;
  int tintby = -1;

  while (!WindowShouldClose()) {
    BeginDrawing();
    ClearBackground(RAYWHITE);

    int amount = (IsKeyDown(KEY_LEFT_SHIFT) ? 1 : 10);
    if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_SPACE)) frame = (frame + amount) % nframes;
    if (IsKeyDown(KEY_LEFT)) frame = (frame + nframes - amount) % nframes;
    if (IsKeyPressed(KEY_DOWN)) frame = (frame + amount) % nframes;
    if (IsKeyPressed(KEY_UP)) frame = (frame + nframes - amount) % nframes;
    int framebase = frame * N;

    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_D)) {
      int rate = 0;
      if (IsKeyDown(KEY_A)) rate -= 1;
      if (IsKeyDown(KEY_D)) rate += 1;
      Vector3 delta = Vector3Scale(
        Vector3Normalize(Vector3CrossProduct(
          Vector3Subtract(camera.target, camera.position),
          camera.up
        )),
        rate * 0.03
      );
      camera.position = Vector3Add(camera.position, delta);
      camera.target = Vector3Add(camera.target, delta);
    }
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_W)) {
      int rate = 0;
      if (IsKeyDown(KEY_S)) rate -= 1;
      if (IsKeyDown(KEY_W)) rate += 1;
      Vector3 delta = Vector3Scale(
        Vector3Normalize(Vector3Subtract(camera.target, camera.position)),
        rate * 0.12
      );
      camera.position = Vector3Add(camera.position, delta);
      camera.target = Vector3Add(camera.target, delta);
    }
    if (IsKeyDown(KEY_Q) || IsKeyDown(KEY_Z)) {
      int rate = 0;
      if (IsKeyDown(KEY_Z)) rate -= 1;
      if (IsKeyDown(KEY_Q)) rate += 1;
      Vector3 delta = Vector3Scale(
        Vector3Normalize(camera.up),
        rate * 0.03
      );
      camera.position = Vector3Add(camera.position, delta);
      camera.target = Vector3Add(camera.target, delta);
    }

    if (IsKeyPressed(KEY_ONE)) forcemask ^= (1 << 0);
    if (IsKeyPressed(KEY_TWO)) forcemask ^= (1 << 1);
    if (IsKeyPressed(KEY_THREE)) forcemask ^= (1 << 2);
    if (IsKeyPressed(KEY_FOUR)) forcemask ^= (1 << 3);
    if (IsKeyPressed(KEY_FIVE)) forcemask ^= (1 << 4);

    if (IsKeyPressed(KEY_ZERO)) tintby = (tintby + 2) % 4 - 1;

    // Find the particle being pointed at
    Ray ray = GetMouseRay(GetMousePosition(), camera);
    float bestdistance = 1e24;
    int bestparticle = -1;
    for (int i = 0; i < N; i++) {
      Vector3 position = (Vector3){
        ps[framebase + i].pos[0],
        ps[framebase + i].pos[1],
        ps[framebase + i].pos[2],
      };
      RayCollision colli = GetRayCollisionSphere(ray, position, phs[i].radius);
      if (colli.hit && colli.distance < bestdistance) {
        bestdistance = colli.distance;
        bestparticle = i;
      }
    }

    // Draw 3D models
    BeginMode3D(camera);
    for (int i = 0; i < N; i++) {
      Vector3 position = (Vector3){
        ps[framebase + i].pos[0],
        ps[framebase + i].pos[1],
        ps[framebase + i].pos[2],
      };
      Color tint = bodycolour[(int)phs[i].body];
      switch (tintby) {
        case 0: // mass
          tint.a = Remap(phs[i].mass, 0, 200, 16, 255);
          break;
        case 1: // elasticity
          tint.a = Remap(phs[i].elas, 0.9, 0.3, 16, 255);
          break;
        case 2: // contact force
        {
          // tint.a = (ps[framebase + i].contact[0] == -1 ? 16 : 255);
          float contactf[3] = {0, 0, 0};
          for (int j = 0; j < 3; j++)
            for (int k = 0; k < 3; k++)
              contactf[k] += ps[framebase + i].force[j][k];
          tint.a = Clamp(Remap(norm3d(contactf), 0, 500, 16, 255), 16, 255);
          break;
        }
        default: break;
      }
      if (i != bestparticle)
        MyDrawSphereWires(position, phs[i].radius, 7, 7, tint);
      else
        DrawSphereEx(position, phs[i].radius, 7, 7, tint);
      for (int j = 0; j < 5; j++) if (forcemask & (1 << j)) {
        Vector3 force = (Vector3){
          ps[framebase + i].force[j][0],
          ps[framebase + i].force[j][1],
          ps[framebase + i].force[j][2],
        };
        DrawLine3D(position,
          Vector3Add(position, Vector3Scale(force, 5e-4)),
          (Color){
            (int)bodycolour[(int)phs[i].body].r * 3 / 4,
            (int)bodycolour[(int)phs[i].body].g * 3 / 4,
            (int)bodycolour[(int)phs[i].body].b * 3 / 4,
            255
          }
        );
      }
    }
    EndMode3D();

    char s[256];
    snprintf(s, sizeof s, "step %04d", frame);
    MyDrawTextLarge(s, 10, 10, 24, BLACK);

    if (tintby != -1) {
      static const char *tinttypes[3] = {
        "mass", "elasticity", "contact force"
      };
      snprintf(s, sizeof s, "tint by %s", tinttypes[tintby]);
      MyDrawText(s, 10, 40, 16, BLACK);
    }

    static const char *forcenames[5] = {
      "repulsive", "damping", "shear", "groundup", "friction"
    };
    if (forcemask != 0) {
      char *ss = s;
      char *end = s + sizeof s;
      *ss = '\0';
      for (int j = 0, first = 1; j < 5; j++) if (forcemask & (1 << j)) {
        ss += strlcat(ss, (first ? "forces: " : ", "), end - ss);
        ss += strlcat(ss, forcenames[j], end - ss);
        first = 0;
      }
      MyDrawText(s, 10, 60, 16, BLACK);
    }

    int ybase = 70;
    int yskip = 20;
    if (bestparticle != -1) {
      Color tint = (Color){
        (int)bodycolour[(int)phs[bestparticle].body].r * 3 / 4,
        (int)bodycolour[(int)phs[bestparticle].body].g * 3 / 4,
        (int)bodycolour[(int)phs[bestparticle].body].b * 3 / 4,
        255
      };
      snprintf(s, sizeof s, "body %d  particle %d",
        (int)phs[bestparticle].body, bestparticle);
      MyDrawText(s, 10, (ybase += yskip), 16, tint);
      snprintf(s, sizeof s, "pos     (%.4f, %.4f, %.4f)",
        ps[framebase + bestparticle].pos[0],
        ps[framebase + bestparticle].pos[1],
        ps[framebase + bestparticle].pos[2]);
      snprintf(s, sizeof s, "vel     (%.4f, %.4f, %.4f)",
        ps[framebase + bestparticle].vel[0],
        ps[framebase + bestparticle].vel[1],
        ps[framebase + bestparticle].vel[2]);
      MyDrawText(s, 10, (ybase += yskip), 16, tint);
      snprintf(s, sizeof s, "radius  %.4f\n", phs[bestparticle].radius);
      MyDrawText(s, 10, (ybase += yskip), 16, tint);
      snprintf(s, sizeof s, "mass    %.4f\n", phs[bestparticle].mass);
      MyDrawText(s, 10, (ybase += yskip), 16, tint);
      snprintf(s, sizeof s, "elast   %.4f\n", phs[bestparticle].elas);
      MyDrawText(s, 10, (ybase += yskip), 16, tint);
      ybase += 10;
      for (int j = 0; j < 5; j++) {
        snprintf(s, sizeof s, "%-10s %.5f", forcenames[j],
          norm3d(ps[framebase + bestparticle].force[j]));
        MyDrawText(s, 10, (ybase += yskip), 16, (Color){
          (int)bodycolour[(int)phs[bestparticle].body].r * 2 / 3,
          (int)bodycolour[(int)phs[bestparticle].body].g * 2 / 3,
          (int)bodycolour[(int)phs[bestparticle].body].b * 2 / 3,
          255
        });
      }
      ybase += 10;
      if (ps[framebase + bestparticle].contact[0] != -1) {
        char *ss = s;
        char *end = s + sizeof s;
        for (int j = 0; j < 3; j++)
          if (ps[framebase + bestparticle].contact[j] != -1) {
            ss += snprintf(ss, end - ss, "%s%d",
              j == 0 ? "contact: " : ", ",
              (int)ps[framebase + bestparticle].contact[j]
            );
          }
        MyDrawText(s, 10, (ybase += yskip), 16, (Color){64, 64, 64, 255});
      }
    }

    EndDrawing();
  }

  CloseWindow();

  return 0;
}