# Hall-Effect-Thruster
Numerical Simulation for a Hall Effect Thruster (Work in progress)

This code is inspired by the outstanding work of Dr. Lubos Breida Particle In Cell Consulting blog, https://www.particleincell.com/blog
The code introduced here in C++ is a finite element method dedicated to solve Poisson's Equation for a low density plasma in the context of a Hall Effect Thruster design.

The principle is based on the DSMC (Direct Simulation Monte Carlo) where the we compute the density of the plasma for each element of the mesh volume. We could briefly have an overview of the tetrahedral unstructured mesh with the pattern below. 
![mesh2](https://user-images.githubusercontent.com/56968382/99896749-89c87e00-2c61-11eb-8729-4641f6056849.png)


The purpose of this project is to modelize in a reduced scale the behavior of the plasma containing different species of the same Xenon element for a magnetostatic problem. 
Indeed, in general, we consider a Hall effect Thruster as an electrostatic problem. The particles are contained in a magnetic field and pushed by the latter.

It is useful to remember that the DSMC method is a Lagrangian Approach to solve a plasma flow. In other terms, we fully modelize our plasma as a mass of particles in which we want to determine their velocities, positions, collisions by momentum exchange and so on.

![streamlines_trans_ts46_x0 12](https://user-images.githubusercontent.com/56968382/99896779-c72d0b80-2c61-11eb-8609-30b47f6a3fae.png)

On the picture above we could see the magnetic field lines that could describe the trajectory that the particles take in order to be pushed from the ionic thruster. 

To compile the code, it is necessary that all the .dat files is inside the same folder as the code Het.cpp. Furthermore the program is coded in the eleventh version of c++ or c++11. Do not hesitate to create a repository of your results inside a folder for all the data you will generate with the code. 

Have fun !

Gabriel


