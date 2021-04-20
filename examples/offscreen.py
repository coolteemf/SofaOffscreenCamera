import Sofa
import SofaRuntime
import Sofa.Core
# import Sofa.Gui
import Sofa.Simulation
import SofaRuntime
from Sofa.constants import *

SofaRuntime.PluginRepository.addFirstPath('/home/francois/Projects/SofaOffscreenCamera/build/v20.12/install/lib')
import SofaOffscreenCamera

def createScene(root):
    root.addObject('RequiredPlugin', pluginName=[
        'SofaBaseMechanics', 'SofaBoundaryCondition', 'SofaEngine', 'SofaImplicitOdeSolver',
        'SofaLoader', 'SofaOpenglVisual', 'SofaSparseSolver', 'SofaSimpleFem', 'SofaOffscreenCamera'
    ])

    root.dt = 1
    root.addChild('beam')

    # Camera
    root.beam.addObject('VisualStyle', displayFlags='showBehavior showVisual')
    root.beam.addObject('OffscreenCamera',
                        name='camera_beam_and_ball',
                        filepath='%s_%i.png',
                        save_frame_before_first_step=True,
                        save_frame_after_each_n_steps=0,
                        position=[-20, 0, 0], lookAt=[0, 0, 0], zNear=0.01, zFar=200, computeZClip=False, projectionType=1)

    # Solver
    root.beam.addObject('StaticSolver', newton_iterations=10)
    root.beam.addObject('SparseLDLSolver')

    # Topology
    root.beam.addObject('RegularGridTopology', name='grid', n=[5, 5, 21], min=[-2, -2, -10], max=[2, 2, 10])
    root.beam.addObject('HexahedronSetTopologyContainer', name='container', src='@grid')

    # Mechanical
    root.beam.addObject('MechanicalObject', name='mo', src='@grid')
    root.beam.addObject('HexahedronFEMForceField', poissonRatio=0, youngModulus=5000, method='large')

    # Fixed boundary
    root.beam.addObject('BoxROI', box=[-2, -2, -10.1, 2, 2, -9.9], drawBoxes=True, name='fixed_face')
    root.beam.addObject('FixedConstraint', indices='@fixed_face.indices')

    # Traction boundary
    root.beam.addObject('BoxROI', box=[-2, -2, 9.9, 2, 2, 10.1], name='traction_face')
    root.beam.addObject('LinearForceField', points='@traction_face.indices', times=[0, 4], forces=[0, -1, 0, 0, -25, 0])

    # Immersed ball
    root.beam.addChild('ball')
    root.beam.ball.addObject('MeshObjLoader', name='loader', filename='mesh/ball.obj', translation=[0, 0, 9])
    root.beam.ball.addObject('OglModel', src='@loader', color='red')
    root.beam.ball.addObject('BarycentricMapping')
    root.beam.ball.addObject('OffscreenCamera',
                             name='camera_only_ball',
                             filepath='%s_%i.png',
                             save_frame_before_first_step=True,
                             save_frame_after_each_n_steps=1,
                             position=[-20, 0, 0], lookAt=[0, 0, 0], zNear=0.01, zFar=200, computeZClip=False, projectionType=1)


if __name__ == "__main__":
    import SofaRuntime

    root = Sofa.Core.Node()
    createScene(root)
    Sofa.Simulation.init(root)
    Sofa.Simulation.initTextures(root)

    camera = root.beam.camera_beam_and_ball
    camera.widthViewport = 512
    camera.heightViewport = 512
    for _ in range(5):
        print(f"animate {_}")
        camera.save_frame(f'frame_{_ + 1}.jpg')
        Sofa.Simulation.animate(root, 1)
        Sofa.Simulation.updateVisual(root)
