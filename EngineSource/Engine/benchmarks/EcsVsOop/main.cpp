#include "EcsVsOop/Workload.h"
#include "EcsVsOop/EcsBackend.h"
#include "EcsVsOop/OopBackend.h"

#include <cstdio>

int main()
{
    using namespace Alice::Bench;

    std::printf("EcsVsOopBench\n");
    std::printf("  sizeof(BenchTransform) = %zu\n", sizeof(BenchTransform));
    std::printf("  sizeof(BenchDecal)     = %zu\n", sizeof(BenchDecal));
    std::printf("  sizeof(BenchAnimation) = %zu\n", sizeof(BenchAnimation));

    {
        EcsBackend ecs;
        for (Alice::EntityId id = 1; id <= 10; ++id)
            ecs.Add(id);

        ecs.Step(1.0f);
        ecs.Remove(5);

        const BenchTransform* t3 = ecs.GetTransform(3);
        const BenchTransform* t5 = ecs.GetTransform(5);
        std::printf("  ECS size=%zu  t3.x=%.3f  t5=%s\n",
            ecs.Size(),
            t3 ? t3->position.x : -1.0f,
            t5 ? "present" : "removed");
    }

    {
        OopBackend oop;
        for (Alice::EntityId id = 1; id <= 10; ++id)
            oop.Add(id);

        oop.Step(1.0f);
        oop.Remove(5);

        const BenchTransform* t3 = oop.GetTransform(3);
        const BenchTransform* t5 = oop.GetTransform(5);
        std::printf("  OOP size=%zu  t3.x=%.3f  t5=%s\n",
            oop.Size(),
            t3 ? t3->position.x : -1.0f,
            t5 ? "present" : "removed");
    }

    return 0;
}
