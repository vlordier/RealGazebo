"""Render a deterministic preview of the generated C-UAS surroundings mesh.

This is an independent mesh preview (OBJ converted from the FBX) so it does
not depend on Unreal's editor viewport or sky material setup.
"""
import vtk

SOURCE = "/private/tmp/bordeaux_preview/Bordeaux_CUAS_Surroundings.obj"
OUTPUT = "/Users/vincent/Work/RealGazebo/bordeaux-cuas-preview.png"

reader = vtk.vtkOBJReader()
reader.SetFileName(SOURCE)
reader.Update()
mesh = reader.GetOutput()
if mesh.GetNumberOfPoints() == 0:
    raise RuntimeError("preview OBJ contains no points")

mapper = vtk.vtkPolyDataMapper()
mapper.SetInputData(mesh)
actor = vtk.vtkActor()
actor.SetMapper(mapper)
actor.GetProperty().SetColor(0.56, 0.60, 0.66)
actor.GetProperty().SetAmbient(0.28)
actor.GetProperty().SetDiffuse(0.72)
actor.GetProperty().SetSpecular(0.12)

renderer = vtk.vtkRenderer()
renderer.SetBackground(0.035, 0.065, 0.11)
renderer.AddActor(actor)

window = vtk.vtkRenderWindow()
window.SetOffScreenRendering(1)
window.SetSize(1920, 1080)
window.AddRenderer(renderer)

bounds = mesh.GetBounds()
cx = 0.5 * (bounds[0] + bounds[1])
cy = 0.5 * (bounds[2] + bounds[3])
cz = 0.5 * (bounds[4] + bounds[5])
span = max(bounds[1] - bounds[0], bounds[3] - bounds[2])

# Oblique survey view: enough height to show the full 6 km x 5 km operating
# envelope while preserving building/road relief.
camera = renderer.GetActiveCamera()
camera.SetPosition(cx + span * 0.70, cy - span * 0.75, cz + span * 0.80)
camera.SetFocalPoint(cx, cy, cz)
camera.SetViewUp(0, 0, 1)
camera.SetViewAngle(42.0)
renderer.ResetCameraClippingRange()

window.Render()
writer = vtk.vtkPNGWriter()
writer.SetFileName(OUTPUT)
grab = vtk.vtkWindowToImageFilter()
grab.SetInput(window)
grab.SetScale(1)
grab.ReadFrontBufferOff()
grab.Update()
writer.SetInputConnection(grab.GetOutputPort())
writer.Write()
print("Rendered", OUTPUT, "points", mesh.GetNumberOfPoints(), "cells", mesh.GetNumberOfCells())
