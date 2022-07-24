# GPU Font Rendering

This is a demonstration of rendering text directly on the GPU using the vector outlines defined by the font.


This demo is based with some modifications on the method described by Will Dobbie in [GPU text rendering with vector textures](http://wdobbie.com/post/gpu-text-rendering-with-vector-textures/).
Other related work includes
[Improved Alpha-Tested Magnification for Vector Textures and Special Effects](https://dl.acm.org/doi/10.1145/1281500.1281665) (signed distance fields) by Chris Green,
[Easy Scalable Text Rendering on the GPU](https://medium.com/@evanwallace/easy-scalable-text-rendering-on-the-gpu-c3f4d782c5ac) by Evan Wallace
and the [Slug Algorithm](https://jcgt.org/published/0006/02/02/) and [Slug Font Rendering Library](https://sluglibrary.com/) by Eric Lengyel.

This technique is useful for rendering large text or rendering text with arbitrary transforms (e.g. in a 3D scene) and produces pixel-perfect and anti-aliased results.
It has a slightly higher GPU runtime cost, but does not require rasterizing glyphs on the CPU.
In contrast to signed distance fields, it preserves sharp corners at all scales.

## Method

A glyph outline is described by one or more closed contours.
Following the TrueType convention, outside contours are oriented in clockwise direction and inside contours are oriented in counterclockwise direction.
In other words, when following the direction of the contour, the filled area is always to the right.

The contours of a glyph are converted into a list of individual quadratic bezier curves, which are uploaded to the GPU.
A quad is generated for each glyph and the pixel shader determines whether each pixel is inside or outside the glyph.
To do this, the winding number of the pixel is calculated by intersecting a ray with the bezier curves.
At every intersection the ray either enters or exits the filled area as determined by the direction of the bezier curve relative to the ray.
At every exit the winding number is increased by one and at every intersection the winding number is decreased by one.
After considering all intersections, the winding number will be non-zero if the pixel is inside the outline.
The direction of the ray does not matter for the winding number computation, but the math can be greatly simplified by using the x-axis as the ray.
Other directions can be achieved by rotating the control points of all the bezier curves so that the ray aligns with the x-axis again.

Anti-aliasing along the ray direction is implemented by considering a window the size of a pixel around the ray origin.
If an intersection falls into this window, then the winding number is changed only fractionally to compute the coverage of the pixel.
(Note, that we have to also consider intersections slightly behind the ray origin now,
but the implementation first calculates any intersection with the x-axis and then verifies the x-position,
so it does not change much.)
For full anti-aliasing we can use multiple rays along different directions (e.g. one along the x-axis and one along the y-axis).

