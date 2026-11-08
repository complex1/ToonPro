#include "reference3d_catalog.h"
#include "reference3d_builtin.h"
#include "reference3d_pose_library.h"
#include <QtTest>
#include <future>

using namespace toon;
class ReferenceCatalogTests final : public QObject {
    Q_OBJECT
private slots:
    void metadataIncludesOriginalModelsAndDistinctNewCategories() {
        const auto catalog = referenceModelCatalog();
        QCOMPARE(catalog.size(), referenceStarterPresets().size() + 5);
        QSet<QString> ids; QHash<ReferenceCatalogKind, int> counts;
        for (const auto &entry : catalog) {
            QVERIFY(!ids.contains(entry.id)); ids.insert(entry.id);
            QVERIFY(!entry.name.isEmpty()); QVERIFY(!entry.description.isEmpty()); ++counts[entry.kind];
            if (entry.kind == ReferenceCatalogKind::Anime || entry.kind == ReferenceCatalogKind::Pet)
                QVERIFY(entry.sourceUrl.startsWith("https://"));
        }
        QCOMPARE(counts[ReferenceCatalogKind::Anime], 2); QCOMPARE(counts[ReferenceCatalogKind::Pet], 3);
        QString error; QVERIFY(!createReferenceCatalogAsset("missing/model", &error)); QVERIFY(!error.isEmpty());
    }
    void preparedModelsAreRealRiggedAssetsAndSerializePortably() {
        ReferenceScene scene; scene.name = "Anime and pets";
        for (const auto &entry : referenceModelCatalog()) {
            if (entry.kind != ReferenceCatalogKind::Anime && entry.kind != ReferenceCatalogKind::Pet) continue;
            QString error; auto asset = createReferenceCatalogAsset(entry.id, &error);
            QVERIFY2(asset, qPrintable(entry.id + ": " + error));
            QVERIFY2(validateReferenceAsset(*asset).isEmpty(), qPrintable(validateReferenceAsset(*asset)));
            QVERIFY(!asset->skins.isEmpty());
            ReferenceInstance instance; instance.id = entry.id; instance.name = entry.name; instance.asset = asset;
            const auto evaluated = evaluateReferenceInstance(instance, &error); QVERIFY2(evaluated, qPrintable(error));
            QVERIFY(evaluated->bounds.valid); QVERIFY(evaluated->bounds.maximum.y() > evaluated->bounds.minimum.y());
            qint64 triangles = 0;
            for (const auto &mesh : asset->meshes) for (const auto &primitive : mesh.primitives) triangles += primitive.indices.size()/3;
            QVERIFY(triangles > 200); QVERIFY(triangles < 100000);
            QVERIFY(referenceBodyShapeParameters(instance).isEmpty()); // Only expose authored capabilities.
            if (entry.kind == ReferenceCatalogKind::Anime) {
                QVERIFY(asset->humanoidBones.contains("head")); QVERIFY(asset->humanoidBones.contains("leftHand"));
                QVERIFY(!asset->expressions.isEmpty());
            } else {
                QVERIFY(!asset->metadata.value("referencePetKind").toString().isEmpty());
            }
            scene.instances.append(instance);
        }
        QString error; const auto bytes = serializeReferenceScene(scene, &error); QVERIFY2(!bytes.isEmpty(), qPrintable(error));
        const auto restored = deserializeReferenceScene(bytes, &error); QVERIFY2(restored, qPrintable(error));
        QCOMPARE(restored->instances.size(), 5);
        for (int i=0;i<5;++i) QCOMPARE(restored->instances[i].asset->id, scene.instances[i].asset->id);
    }
    void preparedCacheSharesExactImmutableAssetAcrossWorkers() {
        auto a = std::async(std::launch::async, [] { return createReferenceCatalogAsset("anime/seed-san"); });
        auto b = std::async(std::launch::async, [] { return createReferenceCatalogAsset("anime/seed-san"); });
        const auto first = a.get(), second = b.get(); QVERIFY(first); QCOMPARE(first.get(), second.get());
        QString error = "stale"; QCOMPARE(createReferenceCatalogAsset("anime/seed-san", &error).get(), first.get()); QVERIFY(error.isEmpty());
    }
    void animeCharactersAcceptTheActualHumanoidPoseLibraryAndExpressions() {
        for (const auto &id : {QString("anime/seed-san"), QString("anime/alicia-solid")}) {
            QString error; ReferenceInstance original; original.id=id; original.asset=createReferenceCatalogAsset(id,&error);
            QVERIFY2(original.asset,qPrintable(error)); original.translation={2,0,3};
            const auto before=evaluateReferenceInstance(original,&error); QVERIFY2(before,qPrintable(error));
            for(const auto &preset:referencePosePresets()) {
                auto posed=original; QVERIFY2(applyReferencePosePreset(preset.id,posed,&error),qPrintable(id+" / "+preset.id+": "+error));
                QCOMPARE(posed.asset.get(),original.asset.get()); QCOMPARE(posed.translation,original.translation);
                const auto evaluated=evaluateReferenceInstance(posed,&error); QVERIFY2(evaluated,qPrintable(error)); QVERIFY(evaluated->bounds.valid);
            }
            bool visibleExpression=false;
            for(const auto &expression:original.asset->expressions) {
                if(expression.bindings.isEmpty())continue;
                auto expressed=original; QVERIFY2(setReferenceExpression(expressed,expression.name,1,&error),qPrintable(error));
                const auto evaluated=evaluateReferenceInstance(expressed,&error); QVERIFY2(evaluated,qPrintable(error));
                for(int p=0;p<before->primitives.size();++p)for(int v=0;v<before->primitives[p].vertices.size();++v)
                    if((before->primitives[p].vertices[v].position-evaluated->primitives[p].vertices[v].position).length()>1e-6f)visibleExpression=true;
                if(visibleExpression)break;
            }
            QVERIFY2(visibleExpression,qPrintable(id+" has no visible expression deformation"));
        }
    }
};
QTEST_MAIN(ReferenceCatalogTests)
#include "test_reference3d_catalog.moc"
