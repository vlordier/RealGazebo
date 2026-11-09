#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

struct Tri { aiVector3D a, b, c; int layer; };

static bool has(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }
static int layerFor(const std::string& n) {
    std::string s = n;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
    if (has(s, "aeroway")) {
        if (s == "aeroway_ae" || s == "aeroway_ap" || s == "aeroway_pa") return -1;
        return 3;
    }
    if (has(s, "building")) return 2;
    if (has(s, "highway")) return 1;
    return -1;
}

static void walk(const aiScene* scene, const aiNode* node, const aiMatrix4x4& parent,
                 std::vector<Tri>& out) {
    aiMatrix4x4 transform = parent * node->mTransformation;
    for (unsigned i = 0; i < node->mNumMeshes; ++i) {
        const aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        int layer = layerFor(mesh->mName.C_Str());
        if (layer < 0) continue;
        for (unsigned f = 0; f < mesh->mNumFaces; ++f) {
            const aiFace& face = mesh->mFaces[f];
            if (face.mNumIndices != 3) continue;
            out.push_back({transform * mesh->mVertices[face.mIndices[0]],
                           transform * mesh->mVertices[face.mIndices[1]],
                           transform * mesh->mVertices[face.mIndices[2]], layer});
        }
    }
    for (unsigned i = 0; i < node->mNumChildren; ++i) walk(scene, node->mChildren[i], transform, out);
}

struct Rgb { uint8_t r, g, b; };
static Rgb color(int layer) {
    if (layer == 3) return {52, 150, 235};
    if (layer == 2) return {180, 141, 98};
    return {95, 100, 110};
}

static float edge(float ax, float ay, float bx, float by, float px, float py) {
    return (px-ax)*(by-ay) - (py-ay)*(bx-ax);
}

static void drawTri(std::vector<Rgb>& img, int w, int h, float x0, float x1, float y0, float y1,
                    const Tri& t) {
    auto project = [&](const aiVector3D& v) {
        return std::pair<float,float>{(v.x-x0)/(x1-x0)*(w-1), (1.0f-(v.z-y0)/(y1-y0))*(h-1)};
    };
    auto p0=project(t.a), p1=project(t.b), p2=project(t.c);
    float minx=std::max(0.0f, std::floor(std::min({p0.first,p1.first,p2.first})));
    float maxx=std::min((float)w-1, std::ceil(std::max({p0.first,p1.first,p2.first})));
    float miny=std::max(0.0f, std::floor(std::min({p0.second,p1.second,p2.second})));
    float maxy=std::min((float)h-1, std::ceil(std::max({p0.second,p1.second,p2.second})));
    float area=edge(p0.first,p0.second,p1.first,p1.second,p2.first,p2.second);
    if (std::fabs(area) < 0.01f) return;
    Rgb c=color(t.layer);
    for (int y=(int)miny; y<=(int)maxy; ++y) for (int x=(int)minx; x<=(int)maxx; ++x) {
        float w0=edge(p1.first,p1.second,p2.first,p2.second,x+0.5f,y+0.5f);
        float w1=edge(p2.first,p2.second,p0.first,p0.second,x+0.5f,y+0.5f);
        float w2=edge(p0.first,p0.second,p1.first,p1.second,x+0.5f,y+0.5f);
        if ((w0>=0 && w1>=0 && w2>=0) || (w0<=0 && w1<=0 && w2<=0)) img[(size_t)y*w+x]=c;
    }
}

static void writePpm(const std::string& path, const std::vector<Rgb>& img, int w, int h) {
    std::ofstream out(path, std::ios::binary); out << "P6\n" << w << " " << h << "\n255\n";
    for (const Rgb& c : img) out.write(reinterpret_cast<const char*>(&c), 3);
}

static void render(const std::string& path, const std::vector<Tri>& tris, float x0, float x1, float y0, float y1, int onlyLayer=0) {
    const int w=2200, h=1400;
    std::vector<Rgb> img((size_t)w*h, {18,27,38});
    // Roads and buildings first, native aeroways last for an unambiguous view.
    for (int layer=1; layer<=3; ++layer) if (onlyLayer==0 || onlyLayer==layer)
        for (const Tri& t : tris) if (t.layer==layer) drawTri(img,w,h,x0,x1,y0,y1,t);
    writePpm(path,img,w,h);
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(argv[1], aiProcess_Triangulate | aiProcess_JoinIdenticalVertices);
    if (!scene) return 3;
    std::vector<Tri> tris;
    walk(scene, scene->mRootNode, aiMatrix4x4(), tris);
    render("/Users/vincent/Work/RealGazebo/bordeaux-native-semantic-overview.ppm", tris, -36000, 32000, -38000, 22000);
    render("/Users/vincent/Work/RealGazebo/bordeaux-native-semantic-detail.ppm", tris, -26000, 14000, -8000, 24000);
    render("/Users/vincent/Work/RealGazebo/bordeaux-native-airport-only.ppm", tris, -26000, 14000, -8000, 24000, 3);
    render("/Users/vincent/Work/RealGazebo/bordeaux-native-airport-close.ppm", tris, -22000, -10000, -5000, 10000);
    render("/Users/vincent/Work/RealGazebo/bordeaux-native-airport-close-only.ppm", tris, -22000, -10000, -5000, 10000, 3);
    std::ofstream stats("/Users/vincent/Work/RealGazebo/bordeaux-native-semantic-preview.txt");
    stats << "triangles=" << tris.size() << "\n";
    for (int i=1;i<=3;++i) {
        stats << "layer" << i << "=" << std::count_if(tris.begin(), tris.end(), [i](const Tri& t){return t.layer==i;}) << "\n";
        bool first = true; float xmin=0,xmax=0,zmin=0,zmax=0;
        for (const Tri& t : tris) if (t.layer==i) for (const aiVector3D* v : {&t.a,&t.b,&t.c}) {
            if (first) xmin=xmax=v->x, zmin=zmax=v->z, first=false;
            else xmin=std::min(xmin,v->x), xmax=std::max(xmax,v->x), zmin=std::min(zmin,v->z), zmax=std::max(zmax,v->z);
        }
        stats << "layer" << i << "_bounds=" << xmin << "," << xmax << "," << zmin << "," << zmax << "\n";
    }
}
