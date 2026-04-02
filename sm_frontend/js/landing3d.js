
import * as THREE from 'three';

const initHero3D = () => {
    const container = document.getElementById('hero-canvas-container');
    if (!container) return;

    // 1. Scene Setup
    const scene = new THREE.Scene();
    const camera = new THREE.PerspectiveCamera(45, container.clientWidth / container.clientHeight, 0.1, 1000);
    camera.position.set(0, 0, 12); 

    const renderer = new THREE.WebGLRenderer({ alpha: true, antialias: true });
    renderer.setSize(container.clientWidth, container.clientHeight);
    renderer.setPixelRatio(window.devicePixelRatio);
    container.appendChild(renderer.domElement);

    // 2. BUILD THE HYPERBOLIC PARABOLOID SADDLE
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

    // 3. INDUSTRIAL MATERIALS (Titanium & Electric Blue)
    const solidMaterial = new THREE.MeshStandardMaterial({ 
        color: 0x111827, // Dark slate/titanium
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

    // --- 4. LIGHTING UPDATE: STRICTLY CYAN AND WHITE/GREY ---
    scene.add(new THREE.AmbientLight(0x0a0f1d, 1.5)); // Dark blue ambient base
    
    // Main bright Cyan Blue light
    const cyanLight = new THREE.DirectionalLight(0x00bfff, 3.5); 
    cyanLight.position.set(5, 5, 5);
    scene.add(cyanLight);
    
    // REMOVED PURPLE. Added a subtle cool-grey fill light for metallic reflections
    const fillLight = new THREE.DirectionalLight(0x94a3b8, 1.0); 
    fillLight.position.set(-5, -5, -2);
    scene.add(fillLight);

    // 5. THE TRAVELING MESH WAVE ANIMATION
    const totalLines = wireframeGeometry.attributes.position.count;
    const windowSize = Math.floor(totalLines * 0.35); 
    let offset = 0;

    const animate = () => {
        requestAnimationFrame(animate);
        
        // Industrial drift
        modelGroup.rotation.x += 0.0008;
        modelGroup.rotation.y += 0.0012;
        modelGroup.rotation.z -= 0.0006;
        modelGroup.position.y = Math.sin(Date.now() * 0.0005) * 0.2; 

        // Scan wave logic
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