`# shader-tool
Hlsl shader preview inspired by [shadertoy](https://www.shadertoy.com).

# Usage
- Currently, you just have to have a `Shader.hlsl` in the working directory and it will try to load it for preview.
- The signature of the entry point is

```cpp
float4 main(float2 p);
```
- It takes in the screen position and returns the color.
- These are the globals that the shader has access to
1. iMouse - .xy is the current cursor position in the window. .zw is the mouse click position
2. iResolution - The width and height of the window client.
3. iTime - .x represents the time since the window started running while .y represents the frame time.
4. iWheel - represents the cumulative mouse wheel in each scroll direction.
- Pressing `F` toggles full screen.
- Pressing `Escape` leaves fullscreen

# Example
```cpp
float smin(float a, float b, float k )
{
  k *= 1.0;
  float r = exp2(-a/k) + exp2(-b/k);
  return -k*log2(r);
}

float Circle(float2 p, float r)
{
  return length(p) - r;
}

float4 main(float2 p)
{
  float t = (0.5 + 0.5 * cos(iTime.x * 3.141));
  float3 Col = float3(0.858823, 0.521568, 0.0196078);
  float r = 0.4;
  float2 uv = (iResolution - 2.0 * p) / iResolution.y;
  float2 um = (iResolution - 2.0 * iMouse.xy) / iResolution.y;
  float d = Circle(uv, r * t);
  d = smin(d, Circle(uv - um, r), 0.41);
  Col *= (1.0 - smoothstep(d, 0.0, 0.002));
  return float4(Col, 1.0);
}
````