# # Use Node 20 to satisfy MongoDB/Mongoose engine requirements
# FROM node:20-bullseye

# # Install the C++ Compiler and OpenCASCADE Libraries for Linux
# RUN apt-get update && apt-get install -y \
#     build-essential \
#     libocct-data-exchange-dev \
#     libocct-foundation-dev \
#     libocct-modeling-algorithms-dev \
#     libocct-modeling-data-dev \
#     libocct-ocaf-dev \
#     libocct-visualization-dev

# # Set the working directory to the top root folder (voronoi_mesh)
# WORKDIR /app

# # Copy essential C++ source files
# COPY cad_test.cpp voronoi_core.cpp voronoi_core.h ./

# # Create the engine folder inside the backend directory
# RUN mkdir -p /app/mesh-backend/engine

# # Compile the C++ Engine for Linux
# RUN g++ -O3 cad_test.cpp voronoi_core.cpp -o /app/mesh-backend/engine/voronoi_mesh \
#     -I/usr/include/opencascade \
#     -lTKSTEP -lTKSTEPBase -lTKIGES -lTKXSBase -lTKShHealing -lTKOffset \
#     -lTKMath -lTKernel -lTKBRep -lTKTopAlgo -lTKGeomBase -lTKGeomAlgo \
#     -lTKG3d -lTKG2d -lTKMesh -lTKPrim -lTKBool -lTKV3d -pthread -no-pie

# # Grant execution permissions to the compiled binary
# RUN chmod +x /app/mesh-backend/engine/voronoi_mesh

# # Change working directory to the Node.js backend
# WORKDIR /app/mesh-backend

# # Copy backend dependency files
# COPY mesh-backend/package*.json ./

# # Install dependencies (Clean install for production)
# RUN npm install --production

# # Copy the rest of the backend code
# COPY mesh-backend/ .

# # Expose the port (Render uses process.env.PORT, but we'll expose 3000 as a default)
# EXPOSE 3000

# # Start the Node.js server
# CMD ["node", "server.js"]


# Use Node 20 to satisfy MongoDB/Mongoose engine requirements
FROM node:20-bullseye

# Install the C++ Compiler and OpenCASCADE Libraries for Linux
RUN apt-get update && apt-get install -y \
    build-essential \
    libocct-data-exchange-dev \
    libocct-foundation-dev \
    libocct-modeling-algorithms-dev \
    libocct-modeling-data-dev \
    libocct-ocaf-dev \
    libocct-visualization-dev

# Set the working directory to the top root folder
WORKDIR /app

# 1. Copy the Engine code from the new sm_engine folder
COPY sm_engine/ ./sm_engine/

# Create the engine folder inside the new backend directory
RUN mkdir -p /app/sm_backend/engine

# Compile the C++ Engine for Linux using the new paths
RUN g++ -O3 sm_engine/cad_test.cpp sm_engine/voronoi_core.cpp -o /app/sm_backend/engine/voronoi_mesh \
    -I/usr/include/opencascade \
    -lTKSTEP -lTKSTEPBase -lTKIGES -lTKXSBase -lTKShHealing -lTKOffset \
    -lTKMath -lTKernel -lTKBRep -lTKTopAlgo -lTKGeomBase -lTKGeomAlgo \
    -lTKG3d -lTKG2d -lTKMesh -lTKPrim -lTKBool -lTKV3d -pthread -no-pie

# Grant execution permissions to the compiled binary
RUN chmod +x /app/sm_backend/engine/voronoi_mesh

# 2. Setup the Backend using the new sm_backend folder
WORKDIR /app/sm_backend

# Copy backend dependency files
COPY sm_backend/package*.json ./

# Install dependencies (Clean install for production)
RUN npm install --production

# Copy the rest of the backend code
COPY sm_backend/ .

# Expose the port
EXPOSE 7860

# Start the Node.js server
CMD ["node", "server.js"]