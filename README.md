# node-dawn
WebGPU for node. Combines official dawn/node headless bindings with a glfw window/surface for rendering
## install
```
npm install node-dawn
```
## use
```
const { GPU } = require('node-dawn')
let gpu = new GPU(["dawn-backend=vulkan","disable-dawn-features=disallow_unsafe_apis"])
let ctx = gpu.createWindow(1500, 1500, "window title", (glfwInputEventName, ...glfwInputEventArgs) => {
    // process window event callbacks here
        if (glfwInputEventName == 'quit') ctx.close()
        })
        let adapter = await gpu.requestAdapter()
        let device = await adapter.requestDevice()
        ctx.configure({ device })
        ```
        At that point you can go on using `gpu` like you got it from `navigator.gpu` and `ctx` like you got it from `canvas.getContext('webgpu')`

Now you can run your webgpu javascript natively and quit jumping through browser developer hoops until it's more stable.

I've found that this is a bit faster than using it through chrome, and that on windows, using the vulkan backend gives me about 1.5x the fps as the d3d12 backend.  Mileage may vary of course.