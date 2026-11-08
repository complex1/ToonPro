#include "reference3d_pose_library.h"
#include <QJsonArray>
#include <QSet>
#include <QtMath>
#include <algorithm>
#include <array>
#include <cmath>

namespace toon {
namespace {
void require(bool condition, const QString &message) { if (!condition) throw message; }
struct Arm { float upperElevation = -78, upperAzimuth = 0, lowerElevation = -70, lowerAzimuth = 45; };
struct Leg { float hip = 0, knee = 4, spread = .015f, lowerSpread = .015f; };
struct BodyRecipe {
    ReferencePosePreset preset;
    Arm leftArm, rightArm;
    Leg leftLeg, rightLeg;
    float lean = 0, sideLean = 0, headYaw = 0, headPitch = 0;
    float rootPitch = 0, rootRoll = 0, lift = 0;
    float leftFootPitch = 0, rightFootPitch = 0;
    bool fists = false;
};
struct Finger { float base = 0, middle = 0, tip = 0, spread = 0; };
struct HandRecipe { ReferencePosePreset preset; std::array<Finger, 5> fingers; float thumbOpposition = 0; };

const QVector<BodyRecipe> &bodies() {
    // Angles describe anatomical directions, not a particular model's local
    // bone axes. +elevation raises an arm; +hip flexion brings a thigh forward.
    static const QVector<BodyRecipe> recipes = [] {
        QVector<BodyRecipe> r;
        const auto add = [&](const char *id, const char *name, const char *category) -> BodyRecipe & {
            BodyRecipe p; p.preset = {QString::fromLatin1(id), QString::fromLatin1(name), QString::fromLatin1(category), false};
            r.append(p); return r.last();
        };
        add("body/relaxed-standing", "Relaxed standing", "Standing");
        auto &p1 = add("body/contrapposto", "Weight shift", "Standing");
        p1.leftLeg = {8, 22, .12f, -.08f}; p1.rightLeg = {-3, 3, .03f, .03f};
        p1.rootRoll = -5; p1.sideLean = 4; p1.headYaw = -12;
        auto &p2 = add("body/arms-crossed", "Arms folded", "Standing");
        p2.leftArm = {-43, 60, 18, 160}; p2.rightArm = {-49, 65, 14, 163}; p2.headYaw = 8;
        auto &p3 = add("body/hands-on-hips", "Hands on hips", "Standing");
        p3.leftArm = p3.rightArm = {-55, 3, -42, 165}; p3.leftLeg.spread = p3.rightLeg.spread = .10f;
        auto &p4 = add("body/seated-chair", "Seated", "Seated");
        p4.leftLeg = p4.rightLeg = {88, 88, .10f, .02f};
        p4.leftArm = p4.rightArm = {-76, 12, -30, 82}; p4.lean = 5; p4.headPitch = -3;
        auto &p5 = add("body/seated-cross-leg", "Seated cross leg", "Seated");
        p5.leftLeg = {87, 100, -.36f, .42f}; p5.rightLeg = {87, 100, -.36f, .42f};
        p5.leftArm = p5.rightArm = {-73, 15, -28, 80}; p5.lean = 10;
        auto &p6 = add("body/crouch", "Crouch", "Ground");
        p6.leftLeg = p6.rightLeg = {105, 140, .24f, .08f}; p6.lean = 24;
        p6.leftArm = p6.rightArm = {-52, 65, -7, 95}; p6.headPitch = -18;
        auto &p7 = add("body/kneel", "Kneeling", "Ground");
        p7.leftLeg = p7.rightLeg = {-8, 82, .07f, .04f}; p7.leftFootPitch=p7.rightFootPitch=180;
        p7.leftArm = p7.rightArm = {-80, 5, -55, 75}; p7.lean = 8;
        auto &p8 = add("body/kneel-one-knee", "One knee down", "Ground");
        p8.leftLeg = {88, 90, .10f, .03f}; p8.rightLeg = {-22, 68, .12f, .04f}; p8.rightFootPitch=180;
        p8.leftArm = {-72, 18, -15, 85}; p8.rightArm = {-79, 5, -70, 35}; p8.lean = 6;
        auto &p9 = add("body/lying-back", "Lying on back", "Ground");
        p9.rootPitch = -90; p9.leftLeg = p9.rightLeg = {0, 3, .08f, .08f};
        p9.leftArm = p9.rightArm = {-68, 0, -66, 5};
        auto &p10 = add("body/lying-side", "Lying on side", "Ground");
        p10.rootRoll = -90; p10.leftLeg = {35, 65, .03f, .03f}; p10.rightLeg = {20, 42, .03f, .03f};
        p10.leftArm = {-30, 75, 15, 105}; p10.rightArm = {25, 70, 75, 105}; p10.headPitch = 8;
        auto &p11 = add("body/walk-contact", "Walking contact", "Movement");
        p11.leftLeg = {26, 8, .025f, .025f}; p11.rightLeg = {-24, 16, .025f, .025f};
        p11.leftArm = {-67, -65, -55, -55}; p11.rightArm = {-60, 65, -40, 80};
        p11.lean = 4; p11.headYaw = 4;
        auto &p12 = add("body/run-stride", "Running stride", "Movement");
        p12.leftLeg = {62, 88, .03f, .03f}; p12.rightLeg = {-32, 112, .035f, .035f};
        p12.leftArm = {-53, -80, 25, 70}; p12.rightArm = {-18, 80, 65, 100};
        p12.lean = 22; p12.headPitch = -16; p12.fists = true;
        auto &p13 = add("body/jump", "Jumping", "Movement");
        p13.leftLeg = {35, 100, .22f, .15f}; p13.rightLeg = {20, 85, .18f, .10f};
        p13.leftArm = {65, 20, 72, 30}; p13.rightArm = {55, 25, 70, 35};
        p13.lean = 7; p13.lift = .14f;
        auto &p14 = add("body/balance-one-leg", "One leg balance", "Balance");
        p14.leftLeg = {0, 3, .01f, .01f}; p14.rightLeg = {65, 105, .18f, .10f};
        p14.leftArm = {4, 0, 2, 0}; p14.rightArm = {10, 10, 5, 5}; p14.sideLean = 5;
        auto &p15 = add("body/reach-up", "Reach overhead", "Reach");
        p15.leftArm = {78, 30, 84, 30}; p15.rightArm = {78, 30, 84, 30}; p15.headPitch = -20;
        auto &p16 = add("body/reach-forward", "Reach forward", "Reach");
        p16.leftArm = {-5, 86, 2, 90}; p16.rightArm = {-5, 86, 2, 90}; p16.lean = 12; p16.headPitch = -8;
        auto &p17 = add("body/reach-side", "Side reach", "Reach");
        p17.leftArm = {38, 0, 40, 0}; p17.rightArm = {-80, 0, -70, 30}; p17.sideLean = -12; p17.headYaw = 35;
        auto &p18 = add("body/boxing-guard", "Boxing guard", "Action");
        p18.leftArm = {-27, 63, 64, 100}; p18.rightArm = {-34, 60, 66, 110};
        p18.leftLeg = {18, 28, .22f, .18f}; p18.rightLeg = {-12, 32, .20f, .15f}; p18.lean = 12; p18.fists = true;
        auto &p19 = add("body/lunge", "Forward lunge", "Action");
        p19.leftLeg = {62, 85, .12f, .07f}; p19.rightLeg = {-38, 12, .13f, .09f};
        p19.leftArm = {-10, 80, 22, 95}; p19.rightArm = {-65, -45, -43, -60}; p19.lean = 18; p19.headPitch = -12;
        return r;
    }();
    return recipes;
}
const QVector<HandRecipe> &hands() {
    static const QVector<HandRecipe> recipes{
        {{"hands/open", "Open hand", "Hands", true}, {{{0,0,0,-12}, {0,0,0,9}, {0,0,0,0}, {0,0,0,-5}, {0,0,0,-12}}}, 0},
        {{"hands/relaxed", "Relaxed hand", "Hands", true}, {{{12,12,8,-5}, {12,20,10,4}, {16,25,14,0}, {20,30,18,-3}, {24,35,22,-6}}}, 5},
        {{"hands/fist", "Fist", "Hands", true}, {{{42,38,35,25}, {78,100,65,0}, {82,105,68,0}, {86,108,70,0}, {88,110,72,0}}}, 28},
        {{"hands/point", "Pointing", "Hands", true}, {{{28,30,20,12}, {0,0,0,0}, {82,105,65,0}, {85,108,68,0}, {88,110,70,0}}}, 18},
        {{"hands/pinch", "Pinch", "Hands", true}, {{{6.75f,6,4.5f,-13.5f}, {36,45,31.5f,6.25f}, {15,25,15,1}, {22,32,20,-4}, {30,40,25,-9}}}, 0},
        {{"hands/grip", "Cylindrical grip", "Hands", true}, {{{28,25,20,8}, {48,65,40,0}, {53,70,43,0}, {58,75,47,0}, {63,78,50,0}}}, 22},
        {{"hands/victory", "Victory", "Hands", true}, {{{35,30,20,18}, {0,0,0,16}, {0,0,0,-11}, {84,105,65,0}, {87,108,68,0}}}, 22},
        {{"hands/thumbs-up", "Thumb extended", "Hands", true}, {{{0,0,0,0}, {78,100,65,0}, {82,105,68,0}, {86,108,70,0}, {88,110,72,0}}}, 0},
        {{"hands/ok", "OK gesture", "Hands", true}, {{{6.75f,6,4.5f,-13.5f}, {48,60,42,6.25f}, {0,5,0,1}, {5,8,3,-5}, {10,15,8,-12}}}, 0},
        {{"hands/three", "Three fingers", "Hands", true}, {{{35,30,20,18}, {0,0,0,12}, {0,0,0,0}, {0,0,0,-10}, {87,108,68,0}}}, 24},
        {{"hands/hook", "Hook grip", "Hands", true}, {{{10,20,10,-8}, {0,95,65,3}, {0,100,68,0}, {0,100,70,-3}, {0,100,70,-6}}}, 5}
    };
    return recipes;
}
const QStringList fingerNames{"Thumb", "Index", "Middle", "Ring", "Little"};
QString fingerName(const QString &side, int finger, int part) {
    static const QStringList thumb{"Metacarpal", "Proximal", "Distal"}, other{"Proximal", "Intermediate", "Distal"};
    return side + fingerNames[finger] + (finger == 0 ? thumb[part] : other[part]);
}
QVector3D position(const QVector<QMatrix4x4> &globals, int index) { return globals[index].map(QVector3D()); }
QQuaternion orientation(const QMatrix4x4 &matrix) {
    QVector3D axes[3]; float lengths[3];
    for (int column = 0; column < 3; ++column) {
        axes[column] = {matrix(0,column), matrix(1,column), matrix(2,column)}; lengths[column] = axes[column].length();
        require(std::isfinite(lengths[column]) && lengths[column] > 1e-8f, "Pose presets require nondegenerate skeleton transforms.");
        axes[column] /= lengths[column];
    }
    const float tolerance = lengths[0] * .0002f;
    require(std::abs(lengths[1]-lengths[0]) <= tolerance && std::abs(lengths[2]-lengths[0]) <= tolerance
        && std::abs(QVector3D::dotProduct(axes[0],axes[1])) < .0002f
        && std::abs(QVector3D::dotProduct(axes[0],axes[2])) < .0002f
        && std::abs(QVector3D::dotProduct(axes[1],axes[2])) < .0002f
        && QVector3D::dotProduct(QVector3D::crossProduct(axes[0],axes[1]),axes[2]) > .999f,
        "Pose presets currently require positive uniform skeleton scales without shear.");
    QMatrix3x3 rotation;
    for (int column = 0; column < 3; ++column) for (int row = 0; row < 3; ++row) rotation(row,column) = axes[column][row];
    return QQuaternion::fromRotationMatrix(rotation).normalized();
}
class PoseBuilder {
public:
    ReferenceInstance work;
    QVector<QMatrix4x4> rest, globals;
    explicit PoseBuilder(const ReferenceInstance &input) : work(input) {
        work.translation = {}; work.rotation = {}; work.scale = {1,1,1};
        auto restInstance = work; restInstance.poseRotations.clear(); restInstance.poseTranslations.clear(); restInstance.poseScales.clear();
        QString problem; const auto evaluated = evaluateReferenceNodeGlobals(restInstance, &problem); require(bool(evaluated), problem); rest = *evaluated;
        refresh();
    }
    void refresh() {
        QString problem; auto result = evaluateReferenceNodeGlobals(work, &problem); require(bool(result), problem); globals = std::move(*result);
    }
    int bone(const QString &semantic, bool required = true) const {
        const int result = work.asset->humanoidBones.value(semantic,-1);
        require(!required || result >= 0, "This pose requires the mapped humanoid joint: " + semantic + '.');
        return result;
    }
    bool descendant(int node, int ancestor) const {
        for (int depth = 0; node >= 0 && depth < 256; ++depth, node = work.asset->nodes[node].parentIndex) if (node == ancestor) return true;
        return false;
    }
    void chain(int first, int second) const {
        require(first != second && descendant(second, first), "The humanoid mapping does not form the required articulated chains.");
        require((position(rest,second)-position(rest,first)).lengthSquared() > 1e-12f, "The mapped pose chain contains a zero-length segment.");
    }
    void clear(int node) { work.poseRotations.remove(node); work.poseTranslations.remove(node); work.poseScales.remove(node); }
    void setWorld(int node, QQuaternion rotation) {
        const int parent = work.asset->nodes[node].parentIndex;
        const auto parentRotation = parent < 0 ? QQuaternion() : orientation(globals[parent]);
        work.poseRotations[node] = (parentRotation.conjugated()*rotation).normalized(); refresh();
    }
    void delta(int node, QVector3D axis, float degrees) {
        require(axis.lengthSquared() > 1e-12f, "The pose has a degenerate anatomical axis.");
        setWorld(node, (QQuaternion::fromAxisAndAngle(axis.normalized(),degrees)*orientation(globals[node])).normalized());
    }
    void aim(int node, int child, QVector3D direction) {
        const auto current = position(globals,child)-position(globals,node);
        require(current.lengthSquared() > 1e-12f && direction.lengthSquared() > 1e-12f, "The pose has a degenerate bone direction.");
        const auto swing = QQuaternion::rotationTo(current.normalized(),direction.normalized());
        setWorld(node,(swing*orientation(globals[node])).normalized());
    }
    void aimRestVector(int node, QVector3D restDirection, QVector3D direction) {
        const auto current = (orientation(globals[node])*orientation(rest[node]).conjugated()).rotatedVector(restDirection);
        setWorld(node,(QQuaternion::rotationTo(current.normalized(),direction.normalized())*orientation(globals[node])).normalized());
    }
    QVector3D posedAxis(int node, QVector3D restAxis) const {
        return (orientation(globals[node])*orientation(rest[node]).conjugated()).rotatedVector(restAxis);
    }
};
bool completeHand(const PoseBuilder &p, const QString &side) {
    if (p.bone(side+"Hand",false) < 0) return false;
    for (int finger=0;finger<5;++finger) for (int part=0;part<3;++part) if (p.bone(fingerName(side,finger,part),false)<0) return false;
    return true;
}
struct HandFrame { QVector3D forward, volar; float chirality; };
HandFrame handFrame(const PoseBuilder &p, const QString &side) {
    const int wrist=p.bone(side+"Hand");
    auto forward=position(p.rest,p.bone(side+"MiddleProximal"))-position(p.rest,wrist); forward.normalize();
    auto across=position(p.rest,p.bone(side+"LittleProximal"))-position(p.rest,p.bone(side+"IndexProximal"));
    across-=forward*QVector3D::dotProduct(across,forward); require(across.lengthSquared()>1e-12f,"The hand needs distinct finger base positions."); across.normalize();
    auto volar=QVector3D::crossProduct(across,forward)*(side=="left" ? 1.f : -1.f);
    const auto authored=p.work.asset->metadata.value("handFrames").toObject().value(side).toObject().value("volar").toArray();
    if (authored.size()==3) {
        const QVector3D explicitVolar(float(authored[0].toDouble()),float(authored[1].toDouble()),float(authored[2].toDouble()));
        require(std::isfinite(explicitVolar.lengthSquared()) && explicitVolar.lengthSquared()>1e-12f,"The authored hand frame is invalid.");
        volar=explicitVolar.normalized();
    }
    // Spread is defined away from the middle finger. Mirrored hands use the
    // opposite signed rotation around the palm normal.
    const float chirality=QVector3D::dotProduct(QVector3D::crossProduct(forward,across),volar)>0 ? -1.f : 1.f;
    return {forward,volar,chirality};
}
void alignThumbGesture(PoseBuilder &p,const QString &side,const HandRecipe &recipe,const HandFrame &frame) {
    const int middle=p.bone(fingerName(side,0,1)),distal=p.bone(fingerName(side,0,2));
    const auto lastRest=position(p.rest,distal)-position(p.rest,middle);
    if (recipe.preset.id!="hands/pinch" && recipe.preset.id!="hands/ok") return;
    const int indexMiddle=p.bone(fingerName(side,1,1)),indexDistal=p.bone(fingerName(side,1,2));
    // Humanoid schemas stop at the distal joint. Estimate the final phalanx
    // from the preceding segment; no fabricated tip node or persistent contact
    // constraint is added to the source rig.
    const auto indexLast=position(p.rest,indexDistal)-position(p.rest,indexMiddle);
    const auto indexExtension=p.globals[indexDistal].mapVector(p.rest[indexDistal].inverted().mapVector(indexLast));
    const auto indexTip=position(p.globals,indexDistal)+indexExtension*.70f;
    // Keep the metacarpal's mild authored opposition. Pulling that palm/web
    // attachment toward a deeply curled index can stretch the skin into a fan
    // even when the numerical tip contact is exact. Contact is solved using
    // the two distal thumb joints, against the softer index gesture above.
    const auto start=position(p.globals,middle); auto target=indexTip-start;
    const float first=(position(p.globals,distal)-start).length();
    const float second=p.globals[distal].mapVector(p.rest[distal].inverted().mapVector(lastRest)).length()*.70f;
    require(first>1e-6f && second>1e-6f && target.lengthSquared()>1e-12f,"The thumb gesture requires measurable finger segments.");
    float distance=std::clamp(target.length(),std::abs(first-second)+1e-6f,first+second-1e-6f); target.normalize();
    auto bend=position(p.globals,distal)-start;
    bend-=target*QVector3D::dotProduct(bend,target);
    if (bend.lengthSquared()<1e-12f) { bend=p.posedAxis(p.bone(side+"Hand"),frame.volar); bend-=target*QVector3D::dotProduct(bend,target); }
    require(bend.lengthSquared()>1e-12f,"The thumb gesture has a degenerate bend plane."); bend.normalize();
    const float along=std::clamp((first*first+distance*distance-second*second)/(2*distance),-first,first);
    const float height=std::sqrt(std::max(0.f,first*first-along*along));
    const auto desiredMiddle=start+target*along+bend*height,tip=start+target*distance;
    p.aim(middle,distal,desiredMiddle-start);
    p.aimRestVector(distal,lastRest,tip-position(p.globals,distal));
}
void applyHand(PoseBuilder &p, const QString &side, const HandRecipe &recipe, const QSet<int> &selected = {0,1,2,3,4}) {
    const int wrist=p.bone(side+"Hand"); QSet<int> seen{wrist};
    for (int finger=0;finger<5;++finger) {
        int parent=wrist;
        for (int part=0;part<3;++part) {
            const int node=p.bone(fingerName(side,finger,part)); require(!seen.contains(node),"The hand mapping reuses a joint for different fingers.");
            seen.insert(node); p.chain(parent,node); if (selected.contains(finger)) p.clear(node); parent=node;
        }
    }
    p.refresh(); const auto frame=handFrame(p,side);
    for (int finger=0;finger<5;++finger) for (int part=0;part<3;++part) {
        if (!selected.contains(finger)) continue;
        const int node=p.bone(fingerName(side,finger,part));
        const auto &f=recipe.fingers[size_t(finger)];
        if (part==0) {
            p.delta(node,p.posedAxis(node,frame.volar),f.spread*frame.chirality);
            if (finger==0) p.delta(node,p.posedAxis(node,frame.forward),recipe.thumbOpposition*(side=="left" ? 1.f : -1.f));
        }
        const int a=p.bone(fingerName(side,finger,part==2 ? 1 : part));
        const int b=p.bone(fingerName(side,finger,part==2 ? 2 : part+1));
        const auto direction=(position(p.rest,b)-position(p.rest,a)).normalized();
        const auto axis=QVector3D::crossProduct(direction,frame.volar).normalized();
        p.delta(node,p.posedAxis(node,axis),part==0 ? f.base : part==1 ? f.middle : f.tip);
    }
    if (selected.size()==5) alignThumbGesture(p,side,recipe,frame);
}
QStringList selectedHands(const PoseBuilder &p, ReferenceHandTarget target) {
    require(target==ReferenceHandTarget::Both || target==ReferenceHandTarget::Left || target==ReferenceHandTarget::Right,"Choose a supported hand target.");
    QStringList result;
    if (target==ReferenceHandTarget::Both) {
        for (const QString &side:{QString("left"),QString("right")}) if (p.bone(side+"Hand",false)>=0) result.append(side);
        require(!result.isEmpty(),"This reference has no mapped hand.");
    } else result.append(target==ReferenceHandTarget::Left ? "left" : "right");
    for (const auto &side:result) require(completeHand(p,side),"This operation requires a complete mapped "+side+" hand with three joints per finger.");
    return result;
}
float twistAngle(QQuaternion rotation,QVector3D axis) {
    rotation.normalize(); const float projection=QVector3D::dotProduct(rotation.vector(),axis.normalized());
    float degrees=qRadiansToDegrees(2*std::atan2(projection,rotation.scalar()));
    while (degrees>180) degrees-=360;
    while (degrees<-180) degrees+=360;
    return degrees;
}
std::array<float,3> curlAngles(int finger) { return finger==0 ? std::array<float,3>{45,40,30} : std::array<float,3>{80,100,70}; }
float splayAngle(int finger) { return finger==0 ? 45.f : 25.f; }
HandRecipe controlledHand(const QHash<QString,QVector2D> &values) {
    HandRecipe recipe;
    for (int finger=0;finger<5;++finger) {
        const auto value=values.value(fingerNames[finger].toLower()); const auto angles=curlAngles(finger);
        recipe.fingers[size_t(finger)]={angles[0]*value.x(),angles[1]*value.x(),angles[2]*value.x(),splayAngle(finger)*value.y()};
    }
    return recipe;
}
struct Frame { QVector3D left,up,forward; };
Frame bodyFrame(const PoseBuilder &p) {
    auto up=position(p.rest,p.bone("head"))-position(p.rest,p.bone("hips"));
    require(up.lengthSquared()>1e-10f,"The humanoid torso has no measurable height."); up.normalize();
    auto left=position(p.rest,p.bone("leftUpperArm"))-position(p.rest,p.bone("rightUpperArm"));
    left-=up*QVector3D::dotProduct(left,up); require(left.lengthSquared()>1e-10f,"The humanoid shoulders do not define a left/right frame."); left.normalize();
    return {left,up,QVector3D::crossProduct(left,up).normalized()};
}
QVector3D armDirection(const Frame &f, float side, float elevation, float azimuth) {
    const float e=qDegreesToRadians(elevation),a=qDegreesToRadians(azimuth);
    return f.left*(side*std::cos(e)*std::cos(a))+f.up*std::sin(e)+f.forward*(std::cos(e)*std::sin(a));
}
void applyBody(PoseBuilder &p, const BodyRecipe &recipe, bool mirror) {
    const QStringList required{"hips","spine","head","leftUpperArm","leftLowerArm","leftHand","rightUpperArm","rightLowerArm","rightHand",
        "leftUpperLeg","leftLowerLeg","leftFoot","rightUpperLeg","rightLowerLeg","rightFoot"};
    QSet<int> unique;
    for (const auto &semantic:required) { const int node=p.bone(semantic); require(!unique.contains(node),"The humanoid body mapping reuses a joint."); unique.insert(node); }
    p.chain(p.bone("hips"),p.bone("spine")); p.chain(p.bone("spine"),p.bone("head"));
    for (const QString &side:{QString("left"),QString("right")}) {
        p.chain(p.bone("spine"),p.bone(side+"UpperArm")); p.chain(p.bone(side+"UpperArm"),p.bone(side+"LowerArm")); p.chain(p.bone(side+"LowerArm"),p.bone(side+"Hand"));
        p.chain(p.bone("hips"),p.bone(side+"UpperLeg")); p.chain(p.bone(side+"UpperLeg"),p.bone(side+"LowerLeg")); p.chain(p.bone(side+"LowerLeg"),p.bone(side+"Foot"));
    }
    QStringList supported=required; supported.append({"chest","upperChest","neck","leftShoulder","rightShoulder","leftToes","rightToes"});
    for (const QString &side:{QString("left"),QString("right")}) for (int f=0;f<5;++f) for (int j=0;j<3;++j) supported.append(fingerName(side,f,j));
    for (const auto &semantic:supported) { const int node=p.bone(semantic,false); if (node>=0) p.clear(node); }
    p.refresh();
    const auto f=bodyFrame(p); const float reflection=mirror ? -1.f : 1.f;
    const auto root=(QQuaternion::fromAxisAndAngle(f.forward,recipe.rootRoll*reflection)*QQuaternion::fromAxisAndAngle(f.left,recipe.rootPitch)).normalized();
    p.setWorld(p.bone("hips"),(root*orientation(p.globals[p.bone("hips")])).normalized());
    const auto lateral=root.rotatedVector(f.left),up=root.rotatedVector(f.up),forward=root.rotatedVector(f.forward);
    p.delta(p.bone("spine"),lateral,recipe.lean);
    p.delta(p.bone("spine"),forward,recipe.sideLean*reflection);
    const auto torso=(QQuaternion::fromAxisAndAngle(forward,recipe.sideLean*reflection)*QQuaternion::fromAxisAndAngle(lateral,recipe.lean)*root).normalized();
    for (const QString &side:{QString("left"),QString("right")}) {
        const bool isLeft=side=="left"; const bool sourceLeft=isLeft!=mirror; const float sign=isLeft ? 1.f : -1.f;
        const auto &arm=sourceLeft ? recipe.leftArm : recipe.rightArm;
        p.aim(p.bone(side+"UpperArm"),p.bone(side+"LowerArm"),torso.rotatedVector(armDirection(f,sign,arm.upperElevation,arm.upperAzimuth)));
        p.aim(p.bone(side+"LowerArm"),p.bone(side+"Hand"),torso.rotatedVector(armDirection(f,sign,arm.lowerElevation,arm.lowerAzimuth)));
        const auto &leg=sourceLeft ? recipe.leftLeg : recipe.rightLeg;
        const float hip=qDegreesToRadians(leg.hip),shin=qDegreesToRadians(leg.hip-leg.knee);
        const auto thigh=f.left*(sign*leg.spread)-f.up*std::cos(hip)+f.forward*std::sin(hip);
        const auto lower=f.left*(sign*leg.lowerSpread)-f.up*std::cos(shin)+f.forward*std::sin(shin);
        p.aim(p.bone(side+"UpperLeg"),p.bone(side+"LowerLeg"),root.rotatedVector(thigh));
        p.aim(p.bone(side+"LowerLeg"),p.bone(side+"Foot"),root.rotatedVector(lower));
        const float footPitch=sourceLeft ? recipe.leftFootPitch : recipe.rightFootPitch;
        const int foot=p.bone(side+"Foot");
        p.setWorld(foot,(root*QQuaternion::fromAxisAndAngle(f.left,footPitch)*orientation(p.rest[foot])).normalized());
        if (completeHand(p,side)) applyHand(p,side,hands()[recipe.fists ? 2 : 1]);
    }
    p.delta(p.bone("head"),up,recipe.headYaw*reflection); p.delta(p.bone("head"),lateral,recipe.headPitch);
    // Grounding is an internal pelvis offset. Instance placement and therefore
    // scene composition stay untouched. This is a static study, not contact IK.
    auto restInstance=p.work; restInstance.poseRotations.clear(); restInstance.poseTranslations.clear(); restInstance.poseScales.clear();
    QString problem; const auto before=evaluateReferenceInstance(restInstance,&problem); require(bool(before),problem);
    const auto after=evaluateReferenceInstance(p.work,&problem); require(bool(after),problem);
    require(before->bounds.valid && after->bounds.valid,"The body pose requires evaluable reference geometry.");
    const float height=before->bounds.maximum.y()-before->bounds.minimum.y();
    const QVector3D offset(0,before->bounds.minimum.y()-after->bounds.minimum.y()+height*recipe.lift,0);
    const int hips=p.bone("hips"),parent=p.work.asset->nodes[hips].parentIndex;
    bool invertible=true; const auto inverse=parent<0 ? QMatrix4x4() : p.globals[parent].inverted(&invertible);
    require(invertible,"The pelvis parent transform cannot be inverted.");
    p.work.poseTranslations[hips]=p.work.asset->nodes[hips].translation+inverse.mapVector(offset); p.refresh();
}
} // namespace
QVector<ReferencePosePreset> referencePosePresets() {
    QVector<ReferencePosePreset> result; result.reserve(bodies().size()+hands().size());
    for (const auto &body:bodies()) result.append(body.preset);
    for (const auto &hand:hands()) result.append(hand.preset);
    return result;
}
bool applyReferencePosePreset(const QString &id, ReferenceInstance &instance, QString *error, ReferenceHandTarget target, bool mirror) {
    try {
        require(!instance.locked && bool(instance.asset),"Unlock and select an articulated reference before applying a pose.");
        const auto invalid=validateReferenceInstance(instance); require(invalid.isEmpty(),invalid);
        require(target==ReferenceHandTarget::Both || target==ReferenceHandTarget::Left || target==ReferenceHandTarget::Right,"Choose a supported hand target.");
        const auto body=std::find_if(bodies().cbegin(),bodies().cend(),[&](const BodyRecipe &r){return r.preset.id==id;});
        const auto hand=std::find_if(hands().cbegin(),hands().cend(),[&](const HandRecipe &r){return r.preset.id==id;});
        require(body!=bodies().cend() || hand!=hands().cend(),"The requested built-in pose does not exist.");
        PoseBuilder p(instance);
        if (body!=bodies().cend()) applyBody(p,*body,mirror);
        else {
            if (mirror && target!=ReferenceHandTarget::Both) target=target==ReferenceHandTarget::Left ? ReferenceHandTarget::Right : ReferenceHandTarget::Left;
            QStringList sides;
            if (target==ReferenceHandTarget::Both) {
                for (const QString &side:{QString("left"),QString("right")}) if (p.bone(side+"Hand",false)>=0) sides.append(side);
                require(!sides.isEmpty(),"This reference has no mapped hand.");
            } else sides.append(target==ReferenceHandTarget::Left ? "left" : "right");
            for (const auto &side:sides) { require(completeHand(p,side),"This pose requires a complete mapped "+side+" hand with three joints per finger."); applyHand(p,side,*hand); }
        }
        p.work.translation=instance.translation; p.work.rotation=instance.rotation; p.work.scale=instance.scale;
        QString problem; require(bool(evaluateReferenceInstance(p.work,&problem)),problem);
        instance=std::move(p.work); if (error) error->clear(); return true;
    } catch (const QString &problem) { if (error) *error=problem; }
      catch (const std::bad_alloc &) { if (error) *error="Not enough memory to apply the reference pose."; }
    return false;
}
QVector<ReferenceHandControl> referenceHandControls(const ReferenceInstance &instance, ReferenceHandTarget target, QString *error) {
    try {
        require(bool(instance.asset),"Select an articulated reference before inspecting its hands.");
        const auto invalid=validateReferenceInstance(instance); require(invalid.isEmpty(),invalid);
        PoseBuilder p(instance); const auto sides=selectedHands(p,target);
        QVector<ReferenceHandControl> controls;
        for (int finger=0;finger<5;++finger) controls.append({fingerNames[finger].toLower(),fingerNames[finger],0,0,false});
        int sideCount=0;
        for (const auto &side:sides) {
            const auto frame=handFrame(p,side); QHash<QString,QVector2D> values;
            for (int finger=0;finger<5;++finger) {
                float numerator=0,denominator=0,spread=0; const auto angles=curlAngles(finger);
                for (int part=0;part<3;++part) {
                    const int node=p.bone(fingerName(side,finger,part));
                    const auto restGlobal=orientation(p.rest[node]);
                    const auto delta=(restGlobal*(p.work.asset->nodes[node].rotation.conjugated()
                        *p.work.poseRotations.value(node,p.work.asset->nodes[node].rotation))*restGlobal.conjugated()).normalized();
                    const int a=p.bone(fingerName(side,finger,part==2 ? 1 : part));
                    const int b=p.bone(fingerName(side,finger,part==2 ? 2 : part+1));
                    const auto direction=(position(p.rest,b)-position(p.rest,a)).normalized();
                    const auto axis=QVector3D::crossProduct(direction,frame.volar).normalized();
                    numerator+=angles[size_t(part)]*twistAngle(delta,axis); denominator+=angles[size_t(part)]*angles[size_t(part)];
                    if (part==0) spread=twistAngle(delta,frame.volar)/(splayAngle(finger)*frame.chirality);
                }
                values.insert(fingerNames[finger].toLower(),{std::clamp(numerator/denominator,0.f,1.f),std::clamp(spread,-1.f,1.f)});
            }
            auto reconstructed=p; applyHand(reconstructed,side,controlledHand(values));
            for (int finger=0;finger<5;++finger) {
                const auto value=values.value(fingerNames[finger].toLower()); auto &control=controls[finger];
                if (sideCount>0 && (std::abs(control.curl/sideCount-value.x())>.005f || std::abs(control.splay/sideCount-value.y())>.005f)) control.approximate=true;
                control.curl+=value.x(); control.splay+=value.y();
                for (int part=0;part<3;++part) {
                    const int node=p.bone(fingerName(side,finger,part)); const auto &rest=p.work.asset->nodes[node];
                    const auto actual=p.work.poseRotations.value(node,rest.rotation).normalized();
                    const auto expected=reconstructed.work.poseRotations.value(node,rest.rotation).normalized();
                    if (std::abs(QQuaternion::dotProduct(actual,expected))<.999657f
                        || p.work.poseTranslations.value(node,rest.translation)!=rest.translation
                        || p.work.poseScales.value(node,rest.scale)!=rest.scale) control.approximate=true;
                }
            }
            ++sideCount;
        }
        for (auto &control:controls) { control.curl/=sideCount; control.splay/=sideCount; }
        if (error) error->clear(); return controls;
    } catch (const QString &problem) { if (error) *error=problem; }
      catch (const std::bad_alloc &) { if (error) *error="Not enough memory to inspect the reference hands."; }
    return {};
}
bool setReferenceHandControls(ReferenceInstance &instance,const QHash<QString,QVector2D> &values,ReferenceHandTarget target,QString *error) {
    try {
        require(!instance.locked && bool(instance.asset),"Unlock and select an articulated reference before posing its hands.");
        const auto invalid=validateReferenceInstance(instance); require(invalid.isEmpty(),invalid);
        require(!values.isEmpty() && values.size()<=5,"Provide at least one supported finger control.");
        QSet<int> selected;
        for (auto it=values.cbegin();it!=values.cend();++it) {
            const int finger=fingerNames.indexOf(it.key().left(1).toUpper()+it.key().mid(1));
            require(finger>=0 && it.key()==fingerNames[finger].toLower(),"The requested finger control does not exist.");
            require(std::isfinite(it.value().x()) && std::isfinite(it.value().y()) && it.value().x()>=0 && it.value().x()<=1
                && it.value().y()>=-1 && it.value().y()<=1,"Hand curl must be in [0,1] and splay in [-1,1]."); selected.insert(finger);
        }
        PoseBuilder p(instance); const auto sides=selectedHands(p,target); const auto recipe=controlledHand(values);
        for (const auto &side:sides) applyHand(p,side,recipe,selected);
        p.work.translation=instance.translation; p.work.rotation=instance.rotation; p.work.scale=instance.scale;
        const auto result=validateReferenceInstance(p.work); require(result.isEmpty(),result);
        instance=std::move(p.work); if (error) error->clear(); return true;
    } catch (const QString &problem) { if (error) *error=problem; }
      catch (const std::bad_alloc &) { if (error) *error="Not enough memory to adjust the reference hands."; }
    return false;
}
} // namespace toon
