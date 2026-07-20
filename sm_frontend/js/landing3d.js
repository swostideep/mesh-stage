
import * as THREE from 'three';

const initHero3D = () => {
    const container = document.getElementById('hero-canvas-container');
    if (!container) return;

    // Scene
    const scene = new THREE.Scene();
    const camera = new THREE.PerspectiveCamera(45, container.clientWidth / container.clientHeight, 0.1, 1000);
    camera.position.set(0, 0, 12); 

    const renderer = new THREE.WebGLRenderer({ alpha: true, antialias: true });
    renderer.setSize(container.clientWidth, container.clientHeight);
    renderer.setPixelRatio(window.devicePixelRatio);
    container.appendChild(renderer.domElement);

    // Hyperbolic paraboloid: a saddle has curvature of both signs, so the
    // wireframe reads as a surface being discretised rather than a flat grid.
    let baseGeometry = new THREE.BoxGeometry(6.5, 6.5, 0.15, 60, 60, 1);
    const pos = baseGeometry.attributes.position;
    for (let i = 0; i < pos.count; i++) {
        const x = pos.getX(i);
        const y = pos.getY(i);
        const z = pos.getZ(i);
        const zOffset = 0.15 * (x * x - y * y);
        pos.setZ(i, z + zOffset);
    }
    baseGeometry.center();
    baseGeometry.computeVertexNormals();

    // Materials
    const solidMaterial = new THREE.MeshStandardMaterial({ 
        color: 0x111827,
        metalness: 0.8,  
        roughness: 0.2,  
        polygonOffset: true, 
        polygonOffsetFactor: 1, 
        polygonOffsetUnits: 1
    });
    const solidMesh = new THREE.Mesh(baseGeometry, solidMaterial);

    const wireframeGeometry = new THREE.WireframeGeometry(baseGeometry);

    const bgWireframeMesh = new THREE.LineSegments(
        wireframeGeometry,
        new THREE.LineBasicMaterial({ color: 0x00bfff, transparent: true, opacity: 0.1, blending: THREE.AdditiveBlending })
    );

    const activeWireframeMesh = new THREE.LineSegments(
        wireframeGeometry,
        new THREE.LineBasicMaterial({ color: 0x00bfff, transparent: true, opacity: 0.9, blending: THREE.AdditiveBlending })
    );

    const nodesMesh = new THREE.Points(
        wireframeGeometry,
        new THREE.PointsMaterial({ color: 0xffffff, size: 0.05, transparent: true, opacity: 1.0 })
    );

    const modelGroup = new THREE.Group();
    modelGroup.add(solidMesh);
    modelGroup.add(bgWireframeMesh); 
    modelGroup.add(activeWireframeMesh); 
    modelGroup.add(nodesMesh);
    modelGroup.rotation.set(0.8, 0.5, -0.4); 
    scene.add(modelGroup);

    // Lighting
    scene.add(new THREE.AmbientLight(0x0a0f1d, 1.5));
    
    // Key light
    const cyanLight = new THREE.DirectionalLight(0x00bfff, 3.5); 
    cyanLight.position.set(5, 5, 5);
    scene.add(cyanLight);
    
    // Cool grey fill, so the metallic shading has something to pick up
    const fillLight = new THREE.DirectionalLight(0x94a3b8, 1.0); 
    fillLight.position.set(-5, -5, -2);
    scene.add(fillLight);

    // A window of edges is drawn at full opacity and swept along the buffer,
    // which reads as a scan line travelling over the surface.
    const totalLines = wireframeGeometry.attributes.position.count;
    const windowSize = Math.floor(totalLines * 0.35); 
    let offset = 0;

    const animate = () => {
        requestAnimationFrame(animate);
        
        // Slow drift
        modelGroup.rotation.x += 0.0008;
        modelGroup.rotation.y += 0.0012;
        modelGroup.rotation.z -= 0.0006;
        modelGroup.position.y = Math.sin(Date.now() * 0.0005) * 0.2; 

        // Advance the scan window
        offset += 160; 
        if (offset > totalLines + windowSize) {
            offset = 0; 
        }

        let start = Math.max(0, offset - windowSize);
        let count = Math.min(totalLines, offset) - start;

        activeWireframeMesh.geometry.setDrawRange(start, count);
        nodesMesh.geometry.setDrawRange(start, count);

        renderer.render(scene, camera);
    };
    animate();

    window.addEventListener('resize', () => {
        if(!container) return;
        camera.aspect = container.clientWidth / container.clientHeight;
        camera.updateProjectionMatrix();
        renderer.setSize(container.clientWidth, container.clientHeight);
    });
};

initHero3D();