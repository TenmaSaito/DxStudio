#include <android/asset_manager.h>
#include <android/input.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "DxStudio", __VA_ARGS__)

namespace {
struct V3 { float x{}, y{}, z{}; };
struct Entity {
    std::string name; int parent = -1; bool quad = false, visible = true;
    V3 position{}, rotation{}, scale{1,1,1}; float color[4]{1,1,1,1};
};
struct Camera { V3 target{}; float yaw{}, pitch=.32175055f, distance=6.32455532f; bool ortho=false; };
struct Scene { Camera camera; std::vector<Entity> entities; };
struct M4 { float v[16]{}; };

M4 Identity() { M4 m{}; m.v[0]=m.v[5]=m.v[10]=m.v[15]=1; return m; }
M4 Multiply(const M4& a, const M4& b) {
    M4 r{};
    for (int c=0;c<4;++c) for(int row=0;row<4;++row)
        for(int k=0;k<4;++k) r.v[c*4+row]+=a.v[k*4+row]*b.v[c*4+k];
    return r;
}
M4 Translate(V3 p) { auto m=Identity();m.v[12]=p.x;m.v[13]=p.y;m.v[14]=p.z;return m; }
M4 Scale(V3 s) { auto m=Identity();m.v[0]=s.x;m.v[5]=s.y;m.v[10]=s.z;return m; }
M4 Rotate(V3 r) {
    const float cx=std::cos(r.x),sx=std::sin(r.x),cy=std::cos(r.y),sy=std::sin(r.y),cz=std::cos(r.z),sz=std::sin(r.z);
    M4 x=Identity(),y=Identity(),z=Identity();
    x.v[5]=cx;x.v[6]=sx;x.v[9]=-sx;x.v[10]=cx;
    y.v[0]=cy;y.v[2]=-sy;y.v[8]=sy;y.v[10]=cy;
    z.v[0]=cz;z.v[1]=sz;z.v[4]=-sz;z.v[5]=cz;
    return Multiply(z,Multiply(y,x));
}
V3 Sub(V3 a,V3 b){return{a.x-b.x,a.y-b.y,a.z-b.z};}
V3 Add(V3 a,V3 b){return{a.x+b.x,a.y+b.y,a.z+b.z};}
V3 Cross(V3 a,V3 b){return{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
float Dot(V3 a,V3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
V3 Normalize(V3 a){float l=std::sqrt(std::max(.000001f,Dot(a,a)));return{a.x/l,a.y/l,a.z/l};}
M4 LookAt(V3 eye,V3 center){
    const V3 f=Normalize(Sub(center,eye)), s=Normalize(Cross(f,{0,1,0})), u=Cross(s,f);
    auto m=Identity();m.v[0]=s.x;m.v[4]=s.y;m.v[8]=s.z;m.v[1]=u.x;m.v[5]=u.y;m.v[9]=u.z;
    m.v[2]=-f.x;m.v[6]=-f.y;m.v[10]=-f.z;m.v[12]=-Dot(s,eye);m.v[13]=-Dot(u,eye);m.v[14]=Dot(f,eye);return m;
}
M4 Perspective(float aspect){const float f=1/std::tan(3.14159265f/8);M4 m{};m.v[0]=f/aspect;m.v[5]=f;m.v[10]=-100.1f/99.9f;m.v[11]=-1;m.v[14]=-20.0f/99.9f;return m;}
M4 Ortho(float aspect,float height){M4 m=Identity();m.v[0]=2/(height*aspect);m.v[5]=2/height;m.v[10]=-2/99.9f;m.v[14]=-1.002f;return m;}

bool LoadScene(AAssetManager* manager, Scene& scene) {
    AAsset* asset=AAssetManager_open(manager,"scene.dxs",AASSET_MODE_BUFFER);
    if(!asset)return false;
    std::string text(static_cast<const char*>(AAsset_getBuffer(asset)),static_cast<size_t>(AAsset_getLength(asset)));AAsset_close(asset);
    std::istringstream in(text);std::string magic;unsigned version=0;size_t count=0;
    if(!(in>>magic>>version>>count)||magic!="DXSCENE"||version<1||version>8||count>10000)return false;
    if(version>=4){int ortho=0;if(!(in>>magic>>ortho>>scene.camera.target.x>>scene.camera.target.y>>scene.camera.target.z>>scene.camera.yaw>>scene.camera.pitch>>scene.camera.distance)||magic!="CAMERA")return false;scene.camera.ortho=ortho!=0;}
    scene.entities.reserve(count);
    for(size_t i=0;i<count;++i){
        Entity e;int quad=0;if(!(in>>std::quoted(e.name)))return false;
        if(version==2){std::string ignored;if(!(in>>std::quoted(ignored)))return false;}
        else if(version>=3){size_t n=0;if(!(in>>n)||n>64)return false;for(size_t j=0;j<n;++j){std::string ignored;if(!(in>>std::quoted(ignored)))return false;}}
        if(version>=5&&!(in>>e.parent))return false;
        unsigned renderer=1;
        if(version>=6){std::string asset;if(!(in>>renderer>>std::quoted(asset))||renderer>(version>=8?4u:version>=7?3u:2u))return false;e.visible=renderer==1||renderer==3||renderer==4;} // 4 = MeshRenderer, drawn as its cube / quad stand-in
        if(version>=7){std::string material,texture;int play=0,animationIndex=0;float animationTime=0;size_t properties=0;
            if(!(in>>std::quoted(material)>>std::quoted(texture)>>play>>animationTime>>animationIndex>>properties)||properties>8192)return false;
            for(size_t p=0;p<properties;++p){std::string component,field,value;if(!(in>>std::quoted(component)>>std::quoted(field)>>std::quoted(value)))return false;}}
        if(!(in>>quad>>e.position.x>>e.position.y>>e.position.z>>e.rotation.x>>e.rotation.y>>e.rotation.z>>e.scale.x>>e.scale.y>>e.scale.z>>e.color[0]>>e.color[1]>>e.color[2]>>e.color[3]))return false;
        e.quad=renderer==3||quad!=0;scene.entities.push_back(e);
    }
    return true;
}

std::vector<Entity> Resolve(const Scene& scene){
    auto out=scene.entities;std::vector<unsigned char> done(out.size());
    auto solve=[&](auto&& self,int i)->void{if(i<0||static_cast<size_t>(i)>=out.size()||done[i])return;const int p=scene.entities[i].parent;if(p>=0&&static_cast<size_t>(p)<out.size()){self(self,p);auto& e=out[i];const auto& q=out[p];
        V3 scaled{e.position.x*q.scale.x,e.position.y*q.scale.y,e.position.z*q.scale.z};
        const auto rm=Rotate(q.rotation);V3 rotated{rm.v[0]*scaled.x+rm.v[4]*scaled.y+rm.v[8]*scaled.z,rm.v[1]*scaled.x+rm.v[5]*scaled.y+rm.v[9]*scaled.z,rm.v[2]*scaled.x+rm.v[6]*scaled.y+rm.v[10]*scaled.z};
        e.position=Add(q.position,rotated);e.rotation=Add(q.rotation,e.rotation);e.scale={q.scale.x*e.scale.x,q.scale.y*e.scale.y,q.scale.z*e.scale.z};}done[i]=1;};
    for(int i=0;i<static_cast<int>(out.size());++i)solve(solve,i);return out;
}

GLuint Shader(GLenum type,const char* code){GLuint s=glCreateShader(type);glShaderSource(s,1,&code,nullptr);glCompileShader(s);GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);if(!ok){char log[512]{};glGetShaderInfoLog(s,512,nullptr,log);LOGE("shader: %s",log);}return s;}
struct App {
    android_app* native{};EGLDisplay display=EGL_NO_DISPLAY;EGLSurface surface=EGL_NO_SURFACE;EGLContext context=EGL_NO_CONTEXT;
    int width{},height{};GLuint program{},vbo{};GLint mvp=-1,color=-1;Scene scene;bool ready=false;float touchX{},touchY{};
};
const float Cube[] = {
 -0.5,-0.5,-0.5, -.5,.5,-.5, .5,.5,-.5, -.5,-.5,-.5, .5,.5,-.5, .5,-.5,-.5,
 .5,-.5,.5, .5,.5,.5, -.5,.5,.5, .5,-.5,.5, -.5,.5,.5, -.5,-.5,.5,
 -.5,-.5,.5,-.5,.5,.5,-.5,.5,-.5,-.5,-.5,.5,-.5,.5,-.5,-.5,-.5,-.5,
 .5,-.5,-.5,.5,.5,-.5,.5,.5,.5,.5,-.5,-.5,.5,.5,.5,.5,-.5,.5,
 -.5,.5,-.5,-.5,.5,.5,.5,.5,.5,-.5,.5,-.5,.5,.5,.5,.5,.5,-.5,
 -.5,-.5,.5,-.5,-.5,-.5,.5,-.5,-.5,-.5,-.5,.5,.5,-.5,-.5,.5,-.5,.5,
 -.5,-.5,0,-.5,.5,0,.5,.5,0,-.5,-.5,0,.5,.5,0,.5,-.5,0};

void Shutdown(App& a);
bool Init(App& a){
    // Every EGL step is checked: an unchecked failure (no matching config on
    // some devices / emulators) used an uninitialised config and crashed.
    a.display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if(a.display==EGL_NO_DISPLAY||!eglInitialize(a.display,nullptr,nullptr)){LOGE("eglInitialize failed: 0x%x",eglGetError());a.display=EGL_NO_DISPLAY;return false;}
    EGLConfig config=nullptr;EGLint count=0,format=0;
    const EGLint attrs[]={EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_SURFACE_TYPE,EGL_WINDOW_BIT,EGL_BLUE_SIZE,8,EGL_GREEN_SIZE,8,EGL_RED_SIZE,8,EGL_DEPTH_SIZE,24,EGL_NONE};
    const EGLint attrs16[]={EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_SURFACE_TYPE,EGL_WINDOW_BIT,EGL_BLUE_SIZE,8,EGL_GREEN_SIZE,8,EGL_RED_SIZE,8,EGL_DEPTH_SIZE,16,EGL_NONE};
    if(!eglChooseConfig(a.display,attrs,&config,1,&count)||count<1){
        if(!eglChooseConfig(a.display,attrs16,&config,1,&count)||count<1){LOGE("no OpenGL ES 3 config: 0x%x",eglGetError());Shutdown(a);return false;}
    }
    eglGetConfigAttrib(a.display,config,EGL_NATIVE_VISUAL_ID,&format);ANativeWindow_setBuffersGeometry(a.native->window,0,0,format);
    a.surface=eglCreateWindowSurface(a.display,config,a.native->window,nullptr);
    if(a.surface==EGL_NO_SURFACE){LOGE("eglCreateWindowSurface failed: 0x%x",eglGetError());Shutdown(a);return false;}
    const EGLint ctx[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};a.context=eglCreateContext(a.display,config,EGL_NO_CONTEXT,ctx);
    if(a.context==EGL_NO_CONTEXT||eglMakeCurrent(a.display,a.surface,a.surface,a.context)==EGL_FALSE){LOGE("OpenGL ES 3 context failed: 0x%x",eglGetError());Shutdown(a);return false;}
    eglQuerySurface(a.display,a.surface,EGL_WIDTH,&a.width);eglQuerySurface(a.display,a.surface,EGL_HEIGHT,&a.height);
    const char* vs="#version 300 es\nlayout(location=0) in vec3 p;uniform mat4 mvp;void main(){gl_Position=mvp*vec4(p,1.0);}";
    const char* fs="#version 300 es\nprecision mediump float;uniform vec4 tint;out vec4 c;void main(){c=tint;}";
    GLuint v=Shader(GL_VERTEX_SHADER,vs),f=Shader(GL_FRAGMENT_SHADER,fs);a.program=glCreateProgram();glAttachShader(a.program,v);glAttachShader(a.program,f);glLinkProgram(a.program);glDeleteShader(v);glDeleteShader(f);a.mvp=glGetUniformLocation(a.program,"mvp");a.color=glGetUniformLocation(a.program,"tint");glGenBuffers(1,&a.vbo);glBindBuffer(GL_ARRAY_BUFFER,a.vbo);glBufferData(GL_ARRAY_BUFFER,sizeof(Cube),Cube,GL_STATIC_DRAW);glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,nullptr);glEnable(GL_DEPTH_TEST);glEnable(GL_CULL_FACE);
    // A scene that does not load is logged, not fatal: the app still runs and
    // draws the background instead of waiting forever on a black screen.
    if(a.scene.entities.empty()){Scene loaded;if(LoadScene(a.native->activity->assetManager,loaded))a.scene=std::move(loaded);else LOGE("scene.dxs could not be read (unsupported version or format)");}
    a.ready=true;return true;
}
void Shutdown(App& a){if(a.display!=EGL_NO_DISPLAY){eglMakeCurrent(a.display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);if(a.context!=EGL_NO_CONTEXT)eglDestroyContext(a.display,a.context);if(a.surface!=EGL_NO_SURFACE)eglDestroySurface(a.display,a.surface);eglTerminate(a.display);}a.ready=false;a.display=EGL_NO_DISPLAY;a.context=EGL_NO_CONTEXT;a.surface=EGL_NO_SURFACE;}
void Draw(App& a){if(!a.ready)return;glViewport(0,0,a.width,a.height);glClearColor(.16f,.31f,.50f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);glUseProgram(a.program);glBindBuffer(GL_ARRAY_BUFFER,a.vbo);const auto& c=a.scene.camera;const float cp=std::cos(c.pitch);V3 eye=Add(c.target,{std::sin(c.yaw)*cp*c.distance,std::sin(c.pitch)*c.distance,-std::cos(c.yaw)*cp*c.distance});const float aspect=static_cast<float>(a.width)/std::max(1,a.height);M4 vp=Multiply(c.ortho?Ortho(aspect,c.distance*(10.0f/6.32455532f)):Perspective(aspect),LookAt(eye,c.target));for(const auto&e:Resolve(a.scene)){if(!e.visible)continue;M4 world=Multiply(Translate(e.position),Multiply(Rotate(e.rotation),Scale(e.scale)));M4 mvp=Multiply(vp,world);glUniformMatrix4fv(a.mvp,1,GL_FALSE,mvp.v);glUniform4fv(a.color,1,e.color);glDrawArrays(GL_TRIANGLES,e.quad?36:0,e.quad?6:36);}eglSwapBuffers(a.display,a.surface);}
void Command(android_app* app,int32_t cmd){auto& a=*static_cast<App*>(app->userData);if(cmd==APP_CMD_INIT_WINDOW&&app->window)Init(a);else if(cmd==APP_CMD_TERM_WINDOW)Shutdown(a);}
int32_t Input(android_app* app,AInputEvent* event){auto& a=*static_cast<App*>(app->userData);if(AInputEvent_getType(event)!=AINPUT_EVENT_TYPE_MOTION)return 0;const int action=AMotionEvent_getAction(event)&AMOTION_EVENT_ACTION_MASK;const float x=AMotionEvent_getX(event,0),y=AMotionEvent_getY(event,0);if(action==AMOTION_EVENT_ACTION_DOWN){a.touchX=x;a.touchY=y;return 1;}if(action==AMOTION_EVENT_ACTION_MOVE){a.scene.camera.yaw-=(x-a.touchX)*.008f;a.scene.camera.pitch=std::clamp(a.scene.camera.pitch+(y-a.touchY)*.008f,-1.5f,1.5f);a.touchX=x;a.touchY=y;return 1;}return 1;}
}

void android_main(android_app* app){App state;state.native=app;app->userData=&state;app->onAppCmd=Command;app->onInputEvent=Input;while(true){int events;android_poll_source* source;while(ALooper_pollOnce(state.ready?0:-1,nullptr,&events,reinterpret_cast<void**>(&source))>=0){if(source)source->process(app,source);if(app->destroyRequested){Shutdown(state);return;}}Draw(state);}}
