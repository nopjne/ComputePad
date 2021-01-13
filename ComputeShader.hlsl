RWTexture2D<float4> output : register(u0);
cbuffer Variables : register(b0)
{
    uint Time;
}; 

struct RayHit {
    float3 Location;
    float3 Reflection;
    float Energy;
    int Object;
    float Result;
};

// Define camera position
static float3 CameraPos = float3(0.0f, 0.0f, -10.0f);
static float3 CameraDir = float3(0.0f, 0.0f, 1.0f);
static float Fov = 80.0f;

// Define a ground plane
static float4 GroundPlane = float4(0.0, -10.0f, 0.0f, 0.0f);
static float4 GroundNormal = float4(0.0f, 1.0f, 0.0f, 1.0f);

// Define a sphere
static float3 gSpherePos = float3(5.0f, 0.0f, 80.0f);
static float SphereRadius = 5.0f;

static float3 SpeherePos2 = float3(12.0f, -8.0f, 50.0f);
static float SphereRadius2 = 8.0f;


// Define a light
static float3 gPointLightPos = float3(-100.0f, 20.0f, 110.0f);
static float3 PointLightColor = float3(1.0f, 1.0f, 1.0f);
static float3 DirLightPos = float3(10.0f, 20.0f, 0.0f);
static float3 DirLightDir = float3(-1.0f, 0.0f, 0.0f);
static float3 DirLightColor = float3(1.0f, 1.0f, 1.0f);
static float DirLightFov = 30.0f;

float RayPlaneIntersect(float3 Start, float3 Direction) {
    // (p -p0) * n = 0
    // l = l0 + ld
    // ((l0 + ld) - p0) * n = 0
    // ((l0 + ld) * n - p0 * n = 0
    // l0 * n + (l * n) d - p0 * n = 0
    // (l * n) * d + l0 * n - p0 * n = 0
    // (l * n) * d + (l0 - p0) * n = 0
    // d + (l0 - p0) * n / (l * n) = 1 / (l * n)
    // (l0 - p0) * n / (l * n) = 1 / (l * n) - d
    // (l0 - p0) * n = - d * (l * n)
    // (p0 - l0) * n / (l * n) = d
    float d = dot(GroundNormal.xyz, Direction);
    if (d >= 0.000001f) return 9999999;
    return dot((GroundPlane.xyz - Start), GroundNormal.xyz) / d;
}

struct SphereHit {
    int HitCount;
    float Dist1;
    float Dist2;
};

SphereHit RaySphereIntersection(
    float3 rayPos, float3 rayDir, 
    float3 spherePos, float sphereRadius)
{
    float3 o_minus_c = rayPos - spherePos;

    float p = dot(rayDir, o_minus_c);
    float q = dot(o_minus_c, o_minus_c) - (sphereRadius * sphereRadius);

    float discriminant = (p * p) - q;
    SphereHit Hit;
    if (discriminant < 0.0f)
    {
        Hit.HitCount = 0;
        return Hit;
    }

    float dRoot = sqrt(discriminant);
    Hit.Dist1 = -p - dRoot;
    Hit.Dist2 = -p + dRoot;

    Hit.HitCount = (discriminant > 1e-7) ? 2 : 1;
    return Hit;
}

RayHit RayTestFirst(float3 Start, float3 Direction)
{
    RayHit Hit;
    float Result = 9999999.0f;
    float Local;
    int Object = -1;
    float3 Normal;
    Hit.Energy = 0.1;

    Hit.Energy = 0.2;
    // Test the ground plane.
    Local = RayPlaneIntersect(Start, Direction);
    if (Local >= 0.0f && abs(Local) < abs(Result)) {
        Result = Local;
        Normal = GroundNormal.xyz;
        Hit.Energy = 0.5;
        Object = 0;
    }

    // Test the sphere.
    float3 SpherePos = gSpherePos + float3(10.0f, 0.0f, 0.0f) * sin(float(Time) / 60);
    SphereHit SphereIntersect;
    SphereIntersect = RaySphereIntersection(Start, Direction, SpherePos, SphereRadius);
    Local = min(SphereIntersect.Dist1, SphereIntersect.Dist2);
    if ((SphereIntersect.HitCount != 0) && (Local > 0.01f) && (abs(Local) < abs(Result))) {
        Result = Local;
        Normal = normalize(((Start + Direction * Result)) - SpherePos);
        Object = 2;
        Hit.Energy = 0.1;
    }

    // Test the sphere.
    SphereIntersect = RaySphereIntersection(Start, Direction, SpeherePos2, SphereRadius2);
    Local = min(SphereIntersect.Dist1, SphereIntersect.Dist2);
    if ((SphereIntersect.HitCount != 0) && (Local > 0.01f) && (abs(Local) < abs(Result))) {
        Result = Local;
        Normal = normalize(((Start + Direction * Result)) - SpeherePos2);
        Object = 3;
        Hit.Energy = 0.1;
    }

    Hit.Object = Object;
    Hit.Location = Start + Direction * Result;
    Hit.Reflection = Normal;
    Hit.Result = Result;
    return Hit;
}

[numthreads(1, 1, 1)]
void Entry(uint3 ThreadId : SV_DispatchThreadID)
{
    float3 RayStart = CameraPos + float3(10.0f * cos(float(Time) / 60), 0.0f, 10.0f * sin(float(Time) / 60));
    float3 RayDir = CameraDir ;//+ float3(cos(float(Time) / 60), 0.0f, 1.0f);

    // Construct the initial direction of the ray.
    RayDir.x += (ThreadId.x / 980.0f) - 0.5f;
    RayDir.y += -((ThreadId.y / 1024.0f) - 0.5f);
    RayDir = normalize(RayDir);

    float4 Result = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float Energy = 1.0f;

    float3 PointLightPos = gPointLightPos + float3(0.0f, 10.0f, -100.0f) * sin(float(Time) / 60);

    // Iterate 10 reflections per ray.
    for (int i = 0; i < 100; i += 1) {

        // Do intersection with the objects in the scene.
        RayHit Hit = RayTestFirst(RayStart, RayDir);
        if ((Hit.Object == -1) || Hit.Result >= 5000) {
            // Do fog calculation.
            // Add a bluer tint based on the rays angle to 0,0,1
            Result += float4(0.3f, 0.3f, 0.4f, 1.0) * saturate((1.0f -dot(float3(0.0f,1.0f,0.0f), normalize(RayDir))) * Energy);
            break;
        }

        // Do a ray cast against the lights.
        float LightDistance = distance(Hit.Location, PointLightPos);
        RayHit LightCast = RayTestFirst(Hit.Location, normalize(PointLightPos - Hit.Location));
        if (LightDistance <= LightCast.Result) {
            // Do light calculation. (This is a HACK and needs a real BRDF)
            float Intensity = 100.0f / LightDistance;
            float3 Color = PointLightColor * (dot(Hit.Reflection, normalize(PointLightPos - Hit.Location))) * Energy * Intensity;
            Result.xyz += Color;
        }

        // Continue along the reflection of the normal.
        RayDir = reflect(RayDir, Hit.Reflection);
        RayStart = Hit.Location;
        Energy -= Hit.Energy;
        if (Energy < 0) {
            break;
        }
    }

    output[ThreadId.xy] = Result;
}