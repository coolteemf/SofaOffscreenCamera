#include "OffscreenCamera.h"
#include "QtDrawToolGL.h"

#include <memory>
#include <utility>

#include <sofa/core/ObjectFactory.h>
#include <sofa/simulation/Node.h>
#include <sofa/simulation/Simulation.h>
#include <sofa/simulation/VisualVisitor.h>

#include <sofa/simulation/events/SimulationInitTexturesDoneEvent.h>
#include <sofa/simulation/AnimateEndEvent.h>

OffscreenCamera::OffscreenCamera()
: p_application(nullptr)
, d_filepath(initData(&d_filepath,
    std::string("screenshot_%s_%i.jpg"),
    "filepath",
    "Path of the image file. The special character set '%s' and '%i' can be used in the file name to specify the camera "
    "name and the step number, respectively. Note that the step number will be 0 before the first step of the simulation, "
    "and i after the ith step has been simulated.",
    true  /*is_displayed_in_gui*/,
    false /*is_read_only*/ ))
, d_save_frame_before_first_step(initData(&d_save_frame_before_first_step,
    false,
    "save_frame_before_first_step",
    "Render the frame once the scene has been initialized completely (before even starting the first step) "
    "and save it into 'filepath'. Default to false",
    true  /*is_displayed_in_gui*/,
    false /*is_read_only*/ ))
, d_save_frame_after_each_n_steps(initData(&d_save_frame_after_each_n_steps,
    static_cast<unsigned int> (0),
    "save_frame_after_each_n_steps",
    "Render the frame after every N steps and save it into 'filepath'. Set to zero to disable."
    "Default to 0",
    true  /*is_displayed_in_gui*/,
    false /*is_read_only*/ ))
{
    if (! QCoreApplication::instance()) {
        // In case we are not inside a Qt application (such as with SofaQt),
        // and a previous OffscreenCamera hasn't created it
        static int argc = 1;
        static char * arg0 = strdup("Offscreen");
        p_application = std::make_unique<QGuiApplication>(argc, &arg0);
        QCoreApplication::processEvents();
    }
}

void OffscreenCamera::init() {
    const auto & width = p_widthViewport.getValue();
    const auto & height = p_heightViewport.getValue();

    QSurfaceFormat format;
    format.setSamples(1);
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    format.setProfile(QSurfaceFormat::CompatibilityProfile);
    format.setOption(QSurfaceFormat::DeprecatedFunctions, true);
    format.setVersion(3, 2);

    p_surface = new QOffscreenSurface;
    p_surface->create();
    p_surface->setFormat(format);
    msg_info() << "Offscreen surface created.";

    // Store the previous context and surface if they exist
    QOpenGLContext * previous_context = QOpenGLContext::currentContext();
    QSurface * previous_surface = previous_context ? previous_context->surface() : nullptr;

    // Create a new context
    p_context = new QOpenGLContext(p_surface);
    p_context->setFormat(format);

    if (previous_context) {
        p_context->setShareContext(previous_context);
        msg_info() << "An OpenGl context already existed. Let's share it.";
    }

    if (not p_context->create()) {
        msg_error() << "Failed to create the OpenGL context";
        return;
    }
    msg_info() << "A new OpenGl context has been created.";

    Base::init();
    computeZ();

    if (not p_context->makeCurrent(p_surface)) {
        msg_error() << "Failed to swap the surface of OpenGL context.";
        return;
    }
    msg_info() << "This new OpenGl context is now the current context.";

    p_framebuffer = new QOpenGLFramebufferObject(width, height, GL_TEXTURE_2D);
    msg_info() << "Framebuffer created.";

    if (not p_framebuffer->bind()) {
        msg_error() << "Failed to bind the OpenGL framebuffer.";
    }

    initGL();

    p_framebuffer->release();

    // Restore the previous surface
    if (previous_context && previous_surface) {
        previous_context->makeCurrent(previous_surface);
    }
}

QImage OffscreenCamera::grab_frame() {
    if (! p_framebuffer) {
        throw std::runtime_error("Framebuffer hasn't been created. Have you run the "
                                 "init() method of the OffscreenCamera component?");
    }

    if (! p_context) {
        throw std::runtime_error("No OpenGL context. Have you run the init() method of the "
                                 "OffscreenCamera component?");
    }
    auto * previous_context = QOpenGLContext::currentContext();
    auto * previous_surface = previous_context ? previous_context->surface() : nullptr;
    if (not p_context->makeCurrent(p_surface)) {
        throw std::runtime_error("Failed to swap the surface of OpenGL context.");
    }

    if (not p_framebuffer->bind()) {
        throw std::runtime_error("Failed to bind the OpenGL framebuffer.");
    }

    const auto & width = p_framebuffer->width();
    const auto & height = p_framebuffer->height();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    GLdouble projectionMatrix[16];
    getOpenGLProjectionMatrix(projectionMatrix);
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMultMatrixd(projectionMatrix);

    GLdouble modelViewMatrix[16];
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    getOpenGLModelViewMatrix(modelViewMatrix);
    glMultMatrixd(modelViewMatrix);

    sofa::core::visual::VisualParams visual_parameters;
    visual_parameters.zNear() = getZNear();
    visual_parameters.zFar() = getZFar();
    visual_parameters.viewport() = sofa::helper::fixed_array<int, 4> (0, 0, width, height);
    visual_parameters.setProjectionMatrix(projectionMatrix);

    glEnable(GL_LIGHTING);
    glEnable(GL_DEPTH_TEST);
    glShadeModel(GL_SMOOTH);
    glColor4f(1, 1, 1, 1);
    glDisable(GL_COLOR_MATERIAL);

    auto * node = dynamic_cast<sofa::simulation::Node*>(getContext());
    auto * root = dynamic_cast<sofa::simulation::Node*>(node->getRoot());

    sofa::core::visual::QtDrawToolGL draw_tool;
    visual_parameters.drawTool() = &draw_tool;
    visual_parameters.setSupported(sofa::core::visual::API_OpenGL);
    visual_parameters.update();

    auto root_visual_managers = root->visualManager;
    for (auto * visual_manager : root_visual_managers) {
        visual_manager->preDrawScene(&visual_parameters);
    }
    bool rendered = false; // true if a manager did the rendering
    for (auto * visual_manager : root_visual_managers) {
        rendered = visual_manager->drawScene(&visual_parameters);
        if (rendered)
            break;
    }

    if (!rendered) {
        visual_parameters.pass() = sofa::core::visual::VisualParams::Std;
        sofa::simulation::VisualDrawVisitor act ( &visual_parameters );
        act.setTags(this->getTags());
        node->execute ( &act );

        visual_parameters.pass() = sofa::core::visual::VisualParams::Transparent;
        sofa::simulation::VisualDrawVisitor act2 ( &visual_parameters );
        act2.setTags(this->getTags());
        node->execute ( &act2 );
    }

    for (auto visual_manager = root_visual_managers.rbegin(); visual_manager != root_visual_managers.rend(); ++visual_manager) {
        (*visual_manager)->postDrawScene(&visual_parameters);
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);

    QImage frame = p_framebuffer->toImage();
    p_context->swapBuffers(p_surface);
    if (previous_context && previous_surface) {
        previous_context->makeCurrent(previous_surface);
    }

    if (not p_framebuffer->release()) {
        throw std::runtime_error("Failed to release the OpenGL framebuffer.");
    }

    return frame;
}

void OffscreenCamera::initGL() {
    static GLfloat light_position[] = {-0.7, 0.3, 0, 1};
    static GLfloat specref[]        = {1, 1, 1, 1};
    static GLfloat ambient_light[]  = {0.5, 0.5, 0.5, 1};
    static GLfloat diffuse_light[]  = {0.9, 0.9, 0.9, 1};
    static GLfloat specular[]       = {1, 1, 1, 1};
    static GLfloat lmodel_ambient[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    static GLfloat lmodel_twoside[] = { GL_FALSE };
    static GLfloat lmodel_local[]   = { GL_FALSE };

    glDepthFunc(GL_LEQUAL);
    glClearDepth(1.0);
    glEnable(GL_NORMALIZE);

    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);

    // Set light model
    glLightModelfv(GL_LIGHT_MODEL_LOCAL_VIEWER, lmodel_local);
    glLightModelfv(GL_LIGHT_MODEL_TWO_SIDE, lmodel_twoside);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, lmodel_ambient);

    // Setup 'light 0'
    glLightfv(GL_LIGHT0, GL_AMBIENT, ambient_light);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse_light);
    glLightfv(GL_LIGHT0, GL_SPECULAR, specular);
    glLightfv(GL_LIGHT0, GL_POSITION, light_position);

    // Enable color tracking
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

    // All materials hereafter have full specular reflectivity with a high shine
    glMaterialfv(GL_FRONT, GL_SPECULAR, specref);
    glMateriali(GL_FRONT, GL_SHININESS, 128);

    glShadeModel(GL_SMOOTH);

    glEnableClientState(GL_VERTEX_ARRAY);

    // Turn on our light and enable color along with the light
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
}

void OffscreenCamera::save_frame(const std::string &filepath) {
    QImage frame = grab_frame();
    frame.save(QString::fromStdString(filepath));
    msg_info() << "Frame saved at " << filepath;
}

void OffscreenCamera::handleEvent(sofa::core::objectmodel::Event * ev) {
    using SimulationInitTexturesDoneEvent= sofa::simulation::SimulationInitTexturesDoneEvent;
    using AnimateEndEvent= sofa::simulation::AnimateEndEvent;

    BaseCamera::handleEvent( ev );

    const auto & save_frame_before_first_step = d_save_frame_before_first_step.getValue();
    const auto & save_frame_after_each_n_steps = d_save_frame_after_each_n_steps.getValue();

    if (SimulationInitTexturesDoneEvent::checkEventType(ev) && d_save_frame_before_first_step.getValue()) {
        const auto & filepath = parse_file_path();
        save_frame(filepath);
    } else if (AnimateEndEvent::checkEventType(ev)) {
        ++p_step_number;

        if (save_frame_after_each_n_steps > 0 && (p_step_number % save_frame_after_each_n_steps) == 0) {
            const auto & filepath = parse_file_path();
            save_frame(filepath);
        }
    }
}

std::string OffscreenCamera::parse_file_path() const {
    std::string filepath = d_filepath.getValue();

    std::vector<std::pair<std::string, std::string>> keys = {
            {"%s", this->getName()},
            {"%i", std::to_string(p_step_number)}
    };
    for (const auto & k : keys) {
        size_t start_pos = 0;
        while((start_pos = filepath.find(k.first, start_pos)) != std::string::npos) {
            filepath.replace(start_pos, k.first.length(), k.second);
            start_pos += k.second.length();
        }
    }

    return filepath;
}

int OffscreenCameraClass = sofa::core::RegisterObject("Offscreen rendering camera.")
    .add< OffscreenCamera >()
;