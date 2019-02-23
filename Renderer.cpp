#include "Renderer.h"

#include "lib/gl_structs.h"

#include "DbAlbumCollection.h"
#include "Engine.h"
#include "TextureCache.h"
#include "config.h"
#include "utils.h"
#include "world_state.h"

#define SELECTION_CENTER INT_MAX  // Selection is an unsigned int, so this is center
#define SELECTION_COVERS 1
#define SELECTION_MIRROR 2

Renderer::Renderer(Engine& engine) : textDisplay(this), engine(engine) {
  glfwSwapInterval(0);
  vSyncEnabled = false;
}

void Renderer::ensureVSync(bool enableVSync) {
  if (vSyncEnabled != enableVSync) {
    vSyncEnabled = enableVSync;
    glfwSwapInterval(enableVSync ? 1 : 0);
  }
}

void Renderer::resizeGlScene(int width, int height) {
  if (height == 0)
    height = 1;
  winWidth = width;
  winHeight = height;

  glViewport(0, 0, width, height);  // Reset The Current Viewport
  setProjectionMatrix();
}

void Renderer::getFrustrumSize(double& right, double& top, double& zNear, double& zFar) {
  zNear = 0.1;
  zFar = 500;
  // Calculate The Aspect Ratio Of The Window
  static const double fov = 45;
  static const double squareLength = tan(deg2rad(fov) / 2) * zNear;
  fovAspectBehaviour weight = engine.coverPos.getAspectBehaviour();
  double aspect = double(winHeight) / winWidth;
  right = squareLength / pow(aspect, double(weight.y));
  top = squareLength * pow(aspect, double(weight.x));
}

void Renderer::setProjectionMatrix(bool pickMatrix, int x, int y) {
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  if (pickMatrix) {
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, static_cast<GLint*>(viewport));
    gluPickMatrix(
        GLdouble(x), GLdouble(viewport[3] - y), 1.0, 1.0, static_cast<GLint*>(viewport));
  }
  double right, top, zNear, zFar;
  getFrustrumSize(right, top, zNear, zFar);
  glFrustum(-right, +right, -top, +top, zNear, zFar);
  glMatrixMode(GL_MODELVIEW);
}

void Renderer::glPushOrthoMatrix() {
  glDisable(GL_DEPTH_TEST);
  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();
  glOrtho(0, winWidth, 0, winHeight, -1, 1);
  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();
}

void Renderer::glPopOrthoMatrix() {
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glMatrixMode(GL_MODELVIEW);
  glPopMatrix();
  glEnable(GL_DEPTH_TEST);
}

std::optional<AlbumInfo> Renderer::albumAtPoint(int x, int y) {
  GLuint buf[256];
  glSelectBuffer(256, static_cast<GLuint*>(buf));
  glRenderMode(GL_SELECT);
  setProjectionMatrix(true, x, y);
  glInitNames();
  drawScene(true);
  GLint hits = glRenderMode(GL_RENDER);
  setProjectionMatrix();
  auto* p = static_cast<GLuint*>(buf);
  GLuint minZ = INFINITE;
  GLuint selectedName = 0;
  for (int i = 0; i < hits; i++) {
    GLuint names = *p;
    p++;
    GLuint z = *p;
    p++;
    p++;
    if ((names > 1) && (z < minZ) && (*p == SELECTION_COVERS)) {
      minZ = z;
      selectedName = *(p + 1);
    }
    p += names;
  }

  if (minZ == INFINITE)
    return std::nullopt;
  int offset = (selectedName - SELECTION_CENTER);
  auto& center = engine.worldState.getCenteredPos();
  if (auto iter = engine.db.iterFromPos(center)) {
    return engine.db.getAlbumInfo(engine.db.moveIterBy(iter.value(), offset));
  } else {
    return std::nullopt;
  }
}

void Renderer::drawMirrorPass() {
  glVectord mirrorNormal = engine.coverPos.getMirrorNormal();
  glVectord mirrorCenter = engine.coverPos.getMirrorCenter();

  double mirrorDist;  // distance from origin
  mirrorDist = mirrorCenter * mirrorNormal;
  glVectord mirrorPos = mirrorDist * mirrorNormal;

  glVectord scaleAxis(1, 0, 0);
  glVectord rotAxis = scaleAxis.cross(mirrorNormal);
  double rotAngle = rad2deg(scaleAxis.intersectAng(mirrorNormal));
  rotAngle = -2 * rotAngle;

  glFogfv(GL_FOG_COLOR, std::array<GLfloat, 4>{GetRValue(cfgPanelBg) / 255.0f,
                                               GetGValue(cfgPanelBg) / 255.0f,
                                               GetBValue(cfgPanelBg) / 255.0f, 1.0f}
                            .data());
  glEnable(GL_FOG);

  glPushMatrix();
  glTranslated(2 * mirrorPos.x, 2 * mirrorPos.y, 2 * mirrorPos.z);
  glScalef(-1.0f, 1.0f, 1.0f);
  glRotated(rotAngle, rotAxis.x, rotAxis.y, rotAxis.z);

  drawCovers();
  glPopMatrix();

  glDisable(GL_FOG);
}

void Renderer::drawMirrorOverlay() {
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  glColor4f(GetRValue(cfgPanelBg) / 255.0f, GetGValue(cfgPanelBg) / 255.0f,
            GetBValue(cfgPanelBg) / 255.0f, 0.60f);

  glShadeModel(GL_FLAT);
  glDisable(GL_TEXTURE_2D);
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glPushMatrix();
  glLoadIdentity();
  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();
  glBegin(GL_QUADS);
  glVertex3i(-1, -1, -1);
  glVertex3i(1, -1, -1);
  glVertex3i(1, 1, -1);
  glVertex3i(-1, 1, -1);
  glEnd();
  glPopMatrix();
  glMatrixMode(GL_MODELVIEW);
  glPopMatrix();
  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_TEXTURE_2D);
  glShadeModel(GL_SMOOTH);
}

pfc::array_t<double> Renderer::getMirrorClipPlane() {
  glVectord mirrorNormal = engine.coverPos.getMirrorNormal();
  glVectord mirrorCenter = engine.coverPos.getMirrorCenter();
  glVectord eye2mCent = (mirrorCenter - engine.coverPos.getCameraPos());

  // Angle at which the mirror normal stands to the eyePos
  double mirrorNormalAngle = rad2deg(eye2mCent.intersectAng(mirrorNormal));
  pfc::array_t<double> clipEq;
  clipEq.set_size(4);
  if (mirrorNormalAngle > 90) {
    clipEq[0] = -mirrorNormal.x;
    clipEq[1] = -mirrorNormal.y;
    clipEq[2] = -mirrorNormal.z;
    clipEq[3] = mirrorNormal * mirrorCenter;
  } else {
    clipEq[0] = mirrorNormal.x;
    clipEq[1] = mirrorNormal.y;
    clipEq[2] = mirrorNormal.z;
    clipEq[3] = -(mirrorNormal * mirrorCenter);
  }
  return clipEq;
}

void Renderer::drawScene(bool selectionPass) {
  glLoadIdentity();
  auto cameraPos = engine.coverPos.getCameraPos();
  auto lookAt = engine.coverPos.getLookAt();
  auto up = engine.coverPos.getUpVector();
  gluLookAt(cameraPos.x, cameraPos.y, cameraPos.z, lookAt.x, lookAt.y, lookAt.z, up.x,
            up.y, up.z);

  pfc::array_t<double> clipEq;

  if (engine.coverPos.isMirrorPlaneEnabled()) {
    clipEq = getMirrorClipPlane();
    if (!selectionPass) {
      glClipPlane(GL_CLIP_PLANE0, clipEq.get_ptr());
      glEnable(GL_CLIP_PLANE0);
      glPushName(SELECTION_MIRROR);
      drawMirrorPass();
      glPopName();
      glDisable(GL_CLIP_PLANE0);
      drawMirrorOverlay();
    }

    // invert the clip equation
    for (int i = 0; i < 4; i++) {
      clipEq[i] *= -1;
    }

    glClipPlane(GL_CLIP_PLANE0, clipEq.get_ptr());
    glEnable(GL_CLIP_PLANE0);
  }

  glPushName(SELECTION_COVERS);
  drawCovers(true);
  glPopName();

  if (engine.coverPos.isMirrorPlaneEnabled()) {
    glDisable(GL_CLIP_PLANE0);
  }
}

void Renderer::drawGui() {
  if (cfgShowAlbumTitle || engine.db.initializing()) {
    std::string albumTitle;
    if (engine.db.initializing()) {
      albumTitle = "Generating Cover Display ...";
    } else if (engine.db.empty()) {
      albumTitle = "No Covers to Display";
    } else {
      DBIter iter = engine.db.iterFromPos(engine.worldState.getTarget()).value();
      albumTitle = engine.db.getAlbumInfo(iter).title;
    }
    textDisplay.displayText(albumTitle, int(winWidth * cfgTitlePosH),
                            int(winHeight * (1 - cfgTitlePosV)), TextDisplay::center,
                            TextDisplay::middle);
  }

  if (cfgShowFps) {
    double avgDur, maxDur, avgCPU, maxCPU;
    engine.fpsCounter.getFPS(avgDur, maxDur, avgCPU, maxCPU);
    std::ostringstream dispStringA;
    std::ostringstream dispStringB;
    dispStringA.flags(std::ios_base::fixed | std::ios_base::right);
    dispStringB.flags(std::ios_base::fixed | std::ios_base::right);
    dispStringA.precision(1);
    dispStringB.precision(1);
    dispStringA << "     FPS: " << std::setw(4) << 1 / avgDur;
    dispStringB << "max ms/f: " << std::setw(5) << (1000 * maxDur);
    dispStringA << "   cpu: " << std::setw(5) << avgCPU * 1000;
    dispStringB << "  max: " << std::setw(5) << maxCPU * 1000;
    textDisplay.displayBitmapText(
        dispStringA.str().c_str(), winWidth - 250, winHeight - 20);
    textDisplay.displayBitmapText(
        dispStringB.str().c_str(), winWidth - 250, winHeight - 35);
  }
}

void Renderer::drawBg() {
  glClearColor(GetRValue(cfgPanelBg) / 255.0f, GetGValue(cfgPanelBg) / 255.0f,
               GetBValue(cfgPanelBg) / 255.0f, 0);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::drawFrame() {
  TRACK_CALL_TEXT("Renderer::drawFrame");
  wasMissingTextures = false;
  drawBg();
  drawScene(false);
  drawGui();
}

void Renderer::drawCovers(bool showTarget) {
  if (cfgHighlightWidth == 0)
    showTarget = false;

  if (engine.db.empty() && !engine.db.initializing())
    return;

  struct Cover {
    const GLTexture* tex;
    float offset;
    int index;
    bool isTarget;
  };
  std::vector<Cover> covers;
  if (engine.db.initializing()) {
    auto tex = &engine.texCache.getLoadingTexture();
    for (int i = engine.coverPos.getFirstCover(); i <= engine.coverPos.getLastCover();
         ++i) {
      covers.push_back(Cover{tex, float(i), i, i == 0});
    }
  } else {
    float centerOffset = engine.worldState.getCenteredOffset();
    // We can assume that iterFromPos succeeds, because we already checked for empty db
    DBIter targetCover = engine.db.iterFromPos(engine.worldState.getTarget()).value();
    DBIter centerCover =
        engine.db.iterFromPos(engine.worldState.getCenteredPos()).value();
    DBIter firstCover =
        engine.db.moveIterBy(centerCover, engine.coverPos.getFirstCover() + 1);
    DBIter lastCover = engine.db.moveIterBy(centerCover, engine.coverPos.getLastCover());
    lastCover++;  // moveIterBy never returns the end() element

    int offset = engine.db.difference(firstCover, centerCover);

    for (DBIter p = firstCover; p != lastCover; ++p, ++offset) {
      float co = -centerOffset + offset;

      auto tex = engine.texCache.getAlbumTexture(p->key);
      if (tex == nullptr) {
        wasMissingTextures = true;
        tex = &engine.texCache.getLoadingTexture();
      }
      covers.push_back(Cover{tex, co, offset, p == targetCover});
    }
  }

  for (Cover cover : covers) {
    cover.tex->bind();
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // calculate darkening
    float g = 1 - (std::min(1.0f, (abs(cover.offset) - 2) / 5)) * 0.5f;
    if (abs(cover.offset) < 2)
      g = 1;
    glColor3f(g, g, g);

    glQuad coverQuad = engine.coverPos.getCoverQuad(cover.offset, cover.tex->getAspect());
    glPushName(SELECTION_CENTER + cover.index);
    glBegin(GL_QUADS);
    glFogCoordf(
        static_cast<GLfloat>(engine.coverPos.distanceToMirror(coverQuad.topLeft)));
    glTexCoord2f(0.0f, 0.0f);
    glVertex3fv(coverQuad.topLeft.as_3fv());

    glFogCoordf(
        static_cast<GLfloat>(engine.coverPos.distanceToMirror(coverQuad.topRight)));
    glTexCoord2f(1.0f, 0.0f);
    glVertex3fv(coverQuad.topRight.as_3fv());

    glFogCoordf(
        static_cast<GLfloat>(engine.coverPos.distanceToMirror(coverQuad.bottomRight)));
    glTexCoord2f(1.0f, 1.0f);
    glVertex3fv(coverQuad.bottomRight.as_3fv());

    glFogCoordf(
        static_cast<GLfloat>(engine.coverPos.distanceToMirror(coverQuad.bottomLeft)));
    glTexCoord2f(0.0f, 1.0f);
    glVertex3fv(coverQuad.bottomLeft.as_3fv());
    glEnd();
    glPopName();

    if (showTarget && cover.isTarget) {
      bool clipPlane = false;
      if (glIsEnabled(GL_CLIP_PLANE0)) {
        glDisable(GL_CLIP_PLANE0);
        clipPlane = true;
      }

      glColor3f(GetRValue(cfgTitleColor) / 255.0f, GetGValue(cfgTitleColor) / 255.0f,
                GetBValue(cfgTitleColor) / 255.0f);

      glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
      glDisable(GL_TEXTURE_2D);

      glLineWidth(GLfloat(cfgHighlightWidth));
      glPolygonOffset(-1.0f, -1.0f);
      glEnable(GL_POLYGON_OFFSET_LINE);

      glEnable(GL_VERTEX_ARRAY);
      glVertexPointer(3, GL_FLOAT, 0, static_cast<void*>(&coverQuad));
      glDrawArrays(GL_QUADS, 0, 4);

      glDisable(GL_POLYGON_OFFSET_LINE);

      glEnable(GL_TEXTURE_2D);

      if (clipPlane)
        glEnable(GL_CLIP_PLANE0);
    }
  }
}
