/* Example of a Finite Element ES-PIC Code for a Hall effect Thruster simulation

   Inspired by the fem-pic code elaborated by 
   Lubos Brieda for Advanced PIC 2015 Lesson 8
   See https://www.particleincell.com/2015/fem-pic/ for more information

   And the Unstructured Mesh Chapter from Plasma Simulation with example written by:
   Dr. Lubos Breida, CRC Press edition Taylor & Francis Group

  To compile and run:
	The mesh data (in text format) should be in the same folder as the program
	mkdir results (to create a folder to stock the results)
	g++ -std=c++11 -O2 Het.cpp -o Het
	Het.exe

	N.B : All modification is quoted with this initials GV with comments given besides this quotation. 
*/

#include <iostream>
#include <fstream>
#include <vector>
#include <stdlib.h>
#include <string>
#include <sstream>
#include <iomanip>
#include <random>
#include <math.h>
#include <chrono>
#include <cstdlib>

using namespace std;

/*constants*/
const double EPS0 = 8.8541878e-12;  /*permittivity of free space*/
const double QE = 1.602e-19;		/*elementary charge*/
const double AMU = 1.660538921e-27;		// kg, atomic mass unit
const double Kb = 1.3806488e-23;        // J/K, boltzmann constant
const double PLASMA_DEN = 1e7;
const double NEUT_DEN = 1e8;
const double ION_VELOCITY = 16000;

/*node type*/
enum NodeType { NORMAL, OPEN, INLET, NEUMANN1, NEUMANN2, NEUMANN3};

/*definition of a node*/
struct Node
{
	Node(double x, double y, double z) { pos[0] = x; pos[1] = y; pos[2] = z; type = NORMAL; }
	double pos[3];	/*node position*/
	NodeType type;
	double volume;	/*node volume*/
};

/*definition of a tetrahedron*/
struct Tetra
{
	int con[4];
	double volume;
	Tetra(int n1, int n2, int n3, int n4) { con[0] = n1; con[1] = n2; con[2] = n3; con[3] = n4; }

	/*data structures to hold precomputed 3x3 determinants*/
	double alpha[4], beta[4], gamma[4], delta[4];

	/*cell connectivity*/
	int cell_con[4];	/*index corresponds to the face opposite the i-th node*/
};

/*definition of a volume*/
struct Volume
{
	vector <Node> nodes;
	vector <Tetra> elements;
};


/*particle*/
struct Particle
{
	double pos[3];
	double vel[3];
	double lc[4]; /*logical coordinates*/
	int cell_index;	/*last cell known to contain this particle*/

};

/*species class*/
class Species
{
public:
	vector<Particle> particles;
	double* den;
	double spwt;	/*specific weight for each species of particles*/
	double mass;
	double charge;
	string name;	/*Name of the particle*/
	double den0;	/*Inlet charge density*/
	double rem;		/*fractional particle reminder*/

	Species(int n_nodes) { den = new double[n_nodes]; rem = 0; }
	~Species() { delete[] den; }
};

/*solver class*/
class FESolver
{
public:
	double** K;		/*global stiffness matrix, should use a sparse matrix*/
	double** J;		/*Jacobian matrix*/
	double* F0;		/*"fh" and "fg" parts of the global force vector*/
	double* F1;		/*"ff" part of the force vector*/

	int* ID;		/*ID[n]=A*/
	int** LM;		/*LM[e][a] location matrix */
	double*** NX;	/*NX[e][a] is a dNa/dx [3] vector*/
	int neq;		/*number of unknowns/equations*/

	/*reference values for the Boltzmann term*/
	double n0;
	double phi0;
	double kTe;

	/*solution*/
	double* d;		/*d[neq] is the solution on the uknown nodes*/
	double* g;		/*g[n] essential boundaries*/
	double* uh;		/*uh[n] solution on nodes, union of d and g*/

	double** ef;	/*ef[e][3] is the electric field in cell e*/

	double* detJ; /*determinant of the jacobian x_xi*/

	FESolver(Volume& volume);	/*constructor, initialized data structures*/
	~FESolver();	/*destructor, frees memory*/

	void startAssembly();	/*clears K and F*/
	void preAssembly();
	void addKe(int e, double ke[4][4]);	/*adds contributions from element stiffness matrix*/
	void addFe(double* F, int e, double fe[4]); /*adds contributions from element force vector*/
	double evalNa(int a, double xi, double eta, double zeta);
	void getNax(double nx[3], int e, int a, double xi, double eta, double zeta);
	void inverse(double M[3][3], double V[3][3]);
	void computePhi(double* ion_den);
	void buildF1Vector(double* ion_den);
	void solveNonLinear(double* d);
	void solveLinear(double** K, double* d, double* F);	/*solves Kd=F for d*/
	void updateEf();

	/*evaluates ef in cell e. Since constant field in cell, just copy*/
	void evalEf(double res[3], int e, double lc[3]) { for (int i = 0; i < 3; i++) res[i] = ef[e][i]; }

	void buildJmatrix();

protected:
	void computeNX();

	Volume& volume;
	int n_nodes;
	int n_elements;	/*save this so we can properly deallocate LM*/

	/*quadrature points*/
	double l[2];
	double W[2];
	int n_int;
};

/*PROTOTYPES*/
double getWallTime();
bool LoadVolumeMesh(const string file_name, Volume& volume);
bool LoadSurfaceMesh(const string file_name, Volume& volume, NodeType node_type);
void OutputMesh(int ts, Species *species, Volume& volume, double* phi, double** ef, double* ion_den);
void OutputParticles(Species *species, vector<Particle>& particles);
void DSMC_MEC_collision(Species* species, Volume& volume, double dt);
double ComputerL(Species* species, vector<Particle>& particles);
double ComputeKE(Species* species, vector<Particle>& particles);
double getMomentum(Species* species, vector<Particle>& particles);
double ComputerL(Species* species, vector<Particle>& particles);
double getPE(vector<Particle>& particles, Volume& volume, FESolver& solver);
double det4(double(*M)[4]);
double det3(double(*M)[3]);
void matVecMultiply(double* y, double** A, double* x, int nu);
void vecVecSubtract(double*, double* v1, double* v2, int nu);
bool Inside(double r[3]);
bool XtoLtet(Particle& part, Volume& volume, bool search = true);
void InjectIons(Species *species, Volume& volume, FESolver& solver, double dt);
void MoveParticles(Species *species, Volume& volume, FESolver& solver, double dt);

const double PI = acos(-1.0);
std::chrono::time_point<std::chrono::high_resolution_clock> time_start;	//time at simulation start
std::mt19937 mt_gen(0);		/*seed*/
std::uniform_real_distribution<double> rnd_dist(0, 1.0);
std::uniform_real_distribution<double> rnd_dist2(0.2, 1.0);
double rnd() { return rnd_dist(mt_gen); }
double rnd2(){ return rnd_dist2(mt_gen); }


/*FESolver*/
FESolver::FESolver(Volume& volume) :volume(volume)
{
	/*count number of unknowns*/
	neq = 0;

	/*OPEN nodes are "h" nodes*/
	for (size_t i = 0; i < volume.nodes.size(); i++)
		if (volume.nodes[i].type == NORMAL ||
			volume.nodes[i].type == OPEN) neq++;

	cout << "There are " << neq << " unknowns" << endl;
	/*allocate neq*neq K matrix*/
	K = new double* [neq];
	for (int i = 0; i < neq; i++) K[i] = new double[neq];
	cout << "Allocated " << neq << "x" << neq << " stiffness matrix" << endl;

	/*allocate neq*neq J matrix*/
	J = new double* [neq];
	for (int i = 0; i < neq; i++) J[i] = new double[neq];
	cout << "Allocated " << neq << "x" << neq << " Jacobian matrix" << endl;

	/*allocate F0 and F1 vectors*/
	F0 = new double[neq];
	F1 = new double[neq];
	cout << "Allocated two " << neq << "x1 force vectors" << endl;

	n_nodes = volume.nodes.size();
	n_elements = volume.elements.size();

	/*allocate ID vector*/
	ID = new int[n_nodes];
	cout << "Allocated " << n_nodes << "x1 ID vector" << endl;

	/*allocate location matrix, n_elements*4 */
	LM = new int* [n_elements];
	for (int e = 0; e < n_elements; e++) LM[e] = new int[4];
	cout << "Allocated " << n_elements << "x4 location matrix" << endl;

	/*allocate NX matrix*/
	NX = new double** [n_elements];
	for (int e = 0; e < n_elements; e++)
	{
		NX[e] = new double* [4];
		for (int a = 0; a < 4; a++) NX[e][a] = new double[3];
	}
	cout << "Allocated " << n_elements << "x4x3 NX matrix" << endl;

	/*solution array*/
	d = new double[neq];
	for (int n = 0; n < neq; n++) d[n] = 0;	/*initial guess*/

	/*allocate memory for g and uh arrays*/
	g = new double[n_nodes];
	uh = new double[n_nodes];
	cout << "Allocated " << n_nodes << "x1 g and uh vector" << endl;

	detJ = new double[n_elements];
	cout << "Allocated " << n_elements << "x1 detJ vector" << endl;

	/*electric field*/
	ef = new double* [n_elements];
	for (int n = 0; n < n_elements; n++)
		ef[n] = new double[3];

	/*set up the ID array
	note valid values are 0 to neq-1 and -1 indicates "g" node*/
	int P = 0;
	for (int n = 0; n < n_nodes; n++)
		if (volume.nodes[n].type == NORMAL ||
			volume.nodes[n].type == OPEN) {ID[n] = P; P++;}
		else ID[n] = -1;	/*dirichlet node*/

	/*now set up the LM matrix*/
	for (int e = 0; e < n_elements; e++)
		for (int a = 0; a < 4; a++)	/*tetrahedra*/
		{
			LM[e][a] = ID[volume.elements[e].con[a]];
		}
	cout << "Built ID and LM matrix" << endl;

	/*set quadrature points*/
	l[0] = -sqrt(1.0 / 3.0); l[1] = sqrt(1.0 / 3.0);
	W[0] = 1; W[1] = 1;
	n_int = 2;

	/*compute NX matrix*/
	computeNX();
}

/*~FESolver, frees memory*/
FESolver::~FESolver()
{
	for (int i = 0; i < neq; i++) { delete[] K[i]; delete[] J[i]; }
	for (int e = 0; e < n_elements; e++) delete[] LM[e];
	for (int e = 0; e < n_elements; e++)
	{
		for (int a = 0; a < 4; a++) delete[] NX[e][a];
		delete NX[e];
	}

	for (int e = 0; e < n_elements; e++)	delete[] ef[e];
	delete[] ef;

	delete[] K;
	delete[] J;
	delete[] LM;
	delete[] F0;
	delete[] F1;
	delete[] NX;
	delete[] ID;
	delete[] uh;
	delete[] d;
	delete[] g;
	delete[] detJ;
}

/*clears K and F*/
void FESolver::startAssembly()
{
	for (int i = 0; i < neq; i++)
		for (int j = 0; j < neq; j++) K[i][j] = 0;

	for (int i = 0; i < neq; i++) {
		F0[i] = 0;
		F1[i] = 0;
	}
}

/*adds contributions from element stiffness matrix*/
void FESolver::addKe(int e, double ke[4][4])
{
	for (int a = 0; a < 4; a++)	/*tetrahedra*/
		for (int b = 0; b < 4; b++)
		{
			int P = LM[e][a];
			int Q = LM[e][b];
			if (P < 0 || Q < 0) continue;	/*skip g nodes*/

			K[P][Q] += ke[a][b];
		}
}

/*adds contributions from element force vector to a global F vector*/
void FESolver::addFe(double* F, int e, double fe[4])
{
	for (int a = 0; a < 4; a++)	/*tetrahedra*/
	{
		int P = LM[e][a];
		if (P < 0) continue;	/*skip g nodes*/

		F[P] += fe[a];
	}
}

/*evaluates shape function a at position (xi,eta,zeta)*/
double FESolver::evalNa(int a, double xi, double eta, double zeta)
{
	switch (a)
	{
	case 0: return xi; break;
	case 1: return eta; break;
	case 2: return zeta; break;
	case 3: return 1 - xi - eta - zeta; break;
	default: return 0;	/*shouldn't happen*/
	}
}

/*returns derivative of N[a] at some logical point
since we are using linear elements, these are constant in each element*/
void FESolver::getNax(double nx[3], int e, int a, double xi, double eta, double zeta)
{
	for (int d = 0; d < 3; d++)
		nx[d] = NX[e][a][d];
}

/*computes derivatives of the shape functions for all elements
constants since using linear elements*/
void FESolver::computeNX()
{
	/*derivatives of the shape functions vs. xi*/
	double na_xi[4][3] = { {1,0,0}, {0,1,0}, {0,0,1}, {-1,-1,-1} };

	for (int e = 0; e < n_elements; e++)
	{
		/*node positions*/
		Tetra& tet = volume.elements[e];

		double x[4][3];
		for (int a = 0; a < 4; a++)
		{
			double* pos = volume.nodes[tet.con[a]].pos;
			for (int d = 0; d < 3; d++) x[a][d] = pos[d];	/*copy*/
		}

		/*compute x_xi matrix*/
		double x_xi[3][3];

		for (int i = 0; i < 3; i++)	/*x/y/z*/
			for (int j = 0; j < 3; j++) /*xi/eta/zeta*/
			{
				x_xi[i][j] = 0;
				for (int a = 0; a < 4; a++)	/*tet node*/
					x_xi[i][j] += na_xi[a][j] * x[a][i];
			}

		/*save det(x_xi)*/
		detJ[e] = det3(x_xi);

		/*compute matrix inverse*/
		double xi_x[3][3];
		inverse(x_xi, xi_x);

		/*evaluate na_x*/
		for (int a = 0; a < 4; a++)
			for (int d = 0; d < 3; d++)
			{
				NX[e][a][d] = 0;
				for (int k = 0; k < 3; k++)
					NX[e][a][d] += na_xi[a][k] * xi_x[k][d];
			}
	}
}

/*compute inverse of a 3x3 matrix using the adjugate method*/
void FESolver::inverse(double M[3][3], double V[3][3])
{
	double a = M[0][0];
	double b = M[0][1];
	double c = M[0][2];
	double d = M[1][0];
	double e = M[1][1];
	double f = M[1][2];
	double g = M[2][0];
	double h = M[2][1];
	double i = M[2][2];

	V[0][0] = (e * i - f * h);
	V[1][0] = -(d * i - f * g);
	V[2][0] = (d * h - e * g);
	V[0][1] = -(b * i - c * h);
	V[1][1] = (a * i - c * g);
	V[2][1] = -(a * h - b * g);
	V[0][2] = (b * f - c * e);
	V[1][2] = -(a * f - c * d);
	V[2][2] = (a * e - b * d);
	double det = a * V[0][0] + b * V[1][0] + c * V[2][0];

	double idet = 0;
	if (fabs(det) < 1e-12) {
		cerr << fabs(det)  <<"Matrix is not invertible, setting to [0]!" << endl;
	}
	else idet = 1 / det;

	/*1/det*/
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			V[i][j] *= idet;
}

/*Newton Rhapson solver, input is the ion density*/
void FESolver::solveNonLinear(double* ion_den)
{
	const double tol = 1e-2;

	/*allocate memory for y*/
	double* y = new double[neq];
	double* G = new double[neq];

	/*clear y values*/
	for (int i = 0; i < neq; i++) y[i] = 0;

	bool converged = false;
	double L2;
	for (int it = 0; it < 10; it++)
	{
		/*builds the "ff" part of the force vector*/
		buildF1Vector(ion_den);

		/*form G=K*d-F*/
		matVecMultiply(G, K, d, neq);	//G=K*d
		vecVecSubtract(G, G, F0, neq); //G=G-F giving us G=K*d-F
		vecVecSubtract(G, G, F1, neq);

		buildJmatrix();

		solveLinear(J, y, G);

		/*now that we have y, update solution */
		for (int n = 0; n < neq; n++) d[n] -= y[n];

		/*compute residue*/
		double sum = 0;
		for (int u = 0; u < neq; u++)
		{
			sum += y[u] * y[u];
		}
		if (L2 < 1e-2)
		{
			cout << " NR converged in " << it + 1 << " iterations with L2=" << setprecision(3) << L2 << endl;
			converged = true;
			break;
		}

	}

	if (!converged) cerr << "NR failed to converge, L2 = " << L2 << endl;
	delete[] y;
	delete[] G;


}

/*builds J matrix for NR solver*/
void FESolver::buildJmatrix()
{
	/*first compute exponential term*/
	double* fp_term = new double[neq];
	double* FP = new double[neq];

	for (int n = 0; n < neq; n++) FP[n] = 0;

	for (int n = 0; n < neq; n++)
	{
		fp_term[n] = -QE / EPS0 * n0 * exp((d[n] - phi0) / kTe) * (1 / kTe);
	}

	/*now set J=K*/
	for (int i = 0; i < neq; i++)
		for (int j = 0; j < neq; j++)
			J[i][j] = K[i][j];

	/*build fprime vector*/
	double fe[4];

	for (int e = 0; e < n_elements; e++)
	{
		for (int a = 0; a < 4; a++)
		{
			double ff = 0;
			int A = LM[e][a];
			if (A >= 0)	/*if unknown node*/
			{
				/*perform quadrature*/
				for (int k = 0; k < n_int; k++)
					for (int j = 0; j < n_int; j++)
						for (int i = 0; i < n_int; i++)
						{
							/*change of limits*/
							double xi = 0.5 * (l[i] + 1);
							double eta = 0.5 * (l[j] + 1);
							double zeta = 0.5 * (l[k] + 1);

							double Na = evalNa(a, xi, eta, zeta);
							ff += fp_term[A] * Na * detJ[e] * W[i] * W[j] * W[k];
						}
				ff *= (1.0 / 8.0);	/*change of limits*/
			}
			fe[a] = ff;
		}

		/*assembly*/
		for (int a = 0; a < 4; a++)	/*tetrahedra*/
		{
			int P = LM[e][a];
			if (P < 0) continue;	/*skip g nodes*/

			FP[P] += fe[a];
		}
	}


	/*subtract diagonal term*/
	for (int u = 0; u < neq; u++)
	{
		J[u][u] -= FP[u];
	}

	delete[] fp_term;
	delete[] FP;
}

/*preassembles the K matrix and "h" and "g" parts of the force vector*/
void FESolver::preAssembly()
{
	/*loop over elements*/
	for (int e = 0; e < n_elements; e++)
	{
		Tetra& tet = volume.elements[e];
		double ke[4][4];

		for (int a = 0; a < 4; a++)
			for (int b = 0; b < 4; b++)
			{
				ke[a][b] = 0;	/*reset*/

				/*perform quadrature*/
				for (int k = 0; k < n_int; k++)
					for (int j = 0; j < n_int; j++)
						for (int i = 0; i < n_int; i++)
						{
							double nax[3], nbx[3];

							double xi = 0.5 * (l[i] + 1);	/*not used!*/
							double eta = 0.5 * (l[j] + 1);
							double zeta = 0.5 * (l[k] + 1);
							getNax(nax, e, a, xi, eta, zeta);
							getNax(nbx, e, b, xi, eta, zeta);

							/*dot product*/
							double dot = 0;
							for (int d = 0; d < 3; d++) dot += nax[d] * nbx[d];
							ke[a][b] += dot * detJ[e] * W[i] * W[j] * W[k];
						}
			}

		/*we now have the ke matrix*/
		addKe(e, ke);

		/*force vector*/
		double fe[4];

		for (int a = 0; a < 4; a++)
		{
			/*second term int(na*h), always zero since support only h=0*/
			double fh = 0;

			/*third term, -sum(kab*qb)*/
			double fg = 0;
			for (int b = 0; b < 4; b++)
			{
				int n = tet.con[b];
				double gb = g[n];
				fg -= ke[a][b] * gb;
			}

			/*combine*/
			fe[a] = fh + fg;
		}
		addFe(F0, e, fe);
	}  /*end of element*/


}

/*computes "ff" part of F*/
void FESolver::buildF1Vector(double* ion_den)
{
	double* f = new double[neq];
	/*start by computing the RHS term on all unknown nodes*/
	for (int n = 0; n < n_nodes; n++)
	{
		int A = ID[n];
		if (A < 0) continue;	/*skip known nodes*/
		f[A] = (QE / EPS0) * (ion_den[n] + n0 * exp((d[A] - phi0) / kTe));
		/*GV based on Mark Santi quasi-neutral potential we can replace
		 double gamma = 1 //if isothermal
		 double gamma = 5/3 // if adibatic 
		 //T0 is initial temperature of our system 
		 d[A] - phi0  by kT0/QE*(gamma/(gamma-1))*((ion_den[n]/n0)^(gamma-1)-1)*/
	}

	/*loop over elements*/
	for (int e = 0; e < n_elements; e++)
	{
		double fe[4];
		for (int a = 0; a < 4; a++)
		{
			/*first term is int(na*f), set to zero for now*/
			double ff = 0;
			int A = LM[e][a];
			if (A >= 0)	/*if unknown node*/
			{
				/*perform quadrature*/
				for (int k = 0; k < n_int; k++)
					for (int j = 0; j < n_int; j++)
						for (int i = 0; i < n_int; i++)
						{
							/*change of limits*/
							double xi = 0.5 * (l[i] + 1);
							double eta = 0.5 * (l[j] + 1);
							double zeta = 0.5 * (l[k] + 1);

							double Na = evalNa(a, xi, eta, zeta);
							ff += f[A] * Na * detJ[e] * W[i] * W[j] * W[k];
						}
				ff *= (1.0 / 8.0);	/*change of limits*/
				fe[a] = ff;
			}
		}

		addFe(F1, e, fe);
	}

	delete[] f;
}

/*simple Gauss-Seidel solver for A*x=b*/
void FESolver::solveLinear(double** A, double* x, double* b)
{
	int it;
	const double tol = 1e-4;
	double L2;

	for (int u = 0; u < neq; u++)
		if (fabs(A[u][u]) < 1e-12)
			cout << "Zero diagonal on " << u << endl;

	bool converged = false;
	for (it = 0; it < 10000; it++)
	{
		for (int u = 0; u < neq; u++)
		{
			/*skip over unused nodes*/
			if (fabs(A[u][u]) < 1e-10) continue;

			double sum = 0;
			for (int v = 0; v < neq; v++)
			{
				if (u == v) continue;
				sum += A[u][v] * x[v];
			}
			x[u] = (b[u] - sum) / A[u][u];
		}

		/*periodically compute residue*/
		if (it % 25 == 0)
		{
			double L = 0;
			for (int u = 0; u < neq; u++)
			{
				double sum = 0;
				for (int v = 0; v < neq; v++)
					sum += A[u][v] * x[v];
				double r = b[u] - sum;
				L += r * r;
			}
			L2 = sqrt(L) / neq;
			if (L2 < tol) { converged = true; break; }
		}
	}

	if (!converged) cerr << " GS failed to converge in " << it << " iterations, " << setprecision(3) << ": L2=" << L2 << endl;

}

/*wrapper for solving the non-linear Poisson's equation*/
void FESolver::computePhi(double* ion_den)
{

	/*solve the system*/
	solveNonLinear(ion_den);

	/*combine d and g to phi*/
	for (int n = 0; n < n_nodes; n++)
	{
		/*zero on non-g nodes*/
		uh[n] = g[n];

		/*is this a non-g node?*/
		int A = ID[n];
		if (A >= 0)
			uh[n] += d[A];
	}
}

/*updates electric field*/
void FESolver::updateEf()
{
	/*interpolate electric field*/
	for (int e = 0; e < n_elements; e++)
	{
		Tetra& tet = volume.elements[e];
		for (int d = 0; d < 3; d++) ef[e][d] = 0;

		for (int a = 0; a < 4; a++)
		{
			int A = tet.con[a];
			double nx[3];
			getNax(nx, e, a, 0.5, 0.5, 0.5);
			/*minus sign since negative gradient*/
			for (int d = 0; d < 3; d++) ef[e][d] -= nx[d] * uh[A];
		}
	}
}


/*helper functions for matrix math, y=A*x */
void matVecMultiply(double* y, double** A, double* x, int nu)
{
	for (int i = 0; i < nu; i++)
	{
		y[i] = 0;
		for (int j = 0; j < nu; j++)
			y[i] += A[i][j] * x[j];
	}
}

/*computes y=v1-v2*/
void vecVecSubtract(double* y, double* v1, double* v2, int nu)
{
	for (int i = 0; i < nu; i++)
		y[i] = v1[i] - v2[i];
}


/**************** MAIN **************************/
int main()
{
	//gv : save starting time point.
	time_start = chrono::high_resolution_clock::now();	
	
	/*instantiate volume*/
	/*GV: Initialize a mesh volume, followed by surface mesh for boundary conditions*/
	Volume volume;
	if (!LoadVolumeMesh("Mesh.dat", volume) ||
		!LoadSurfaceMesh("inlet.dat", volume, INLET) ||
		!LoadSurfaceMesh("Neumann1.dat", volume, NEUMANN1)||
		!LoadSurfaceMesh("Neumann2.dat", volume, NEUMANN2) ||
		!LoadSurfaceMesh("Neumann3.dat", volume, NEUMANN3)) return -1;

	/*instantiate solver*/
	FESolver solver(volume);

	/*set reference paramaters*/
	solver.phi0 = 0;
	solver.n0 = PLASMA_DEN;
	solver.kTe = 1.5; 
	
	/*new particle*/
	Particle part;

	int n_elements = volume.elements.size();
	int n_nodes = volume.nodes.size();


	/*initialize solver "g" array*/
	/*GV: Assumption: the wall potential is rendered null */
	for (int n = 0; n < n_nodes; n++)
	{
		if (volume.nodes[n].type == INLET) solver.g[n] = 0;	/*phi_Dirichlet*/
		else if (volume.nodes[n].type == NEUMANN1) solver.g[n] = -20; /*phi_wall*/
		else if (volume.nodes[n].type == NEUMANN2) solver.g[n] = -20; /*phi_wall*/
		else if (volume.nodes[n].type == NEUMANN3) solver.g[n] = 0; /*phi_wall*/
		else solver.g[n] = 0;	/*default*/
	}

	/*sample assembly code*/
	solver.startAssembly();
	solver.preAssembly();	/*this will form K and F0*/

	double dt = 1e-7;

	/*GV: introduction of all the species inside the Hall Effect Thruster (HET)*/

	/*ions species Xe+*/
	Species ions(n_nodes);
	ions.charge = 1 * QE;
	ions.mass = 131 * AMU;
	ions.spwt = 1e2;			/*GV: the specific weight is adapted according to our needs for the results*/
	ions.den0 = 0.8 * PLASMA_DEN;
	ions.name = "Xe+";

	/*ions species Xe++*/
	Species ions2(n_nodes);
	ions2.charge = 2 * QE;
	ions2.mass = 131 * AMU;
	ions2.spwt = 2e2;			/*GV: the specific weight is adapted according to our needs for the results*/
	ions2.den0 = 0.1 * PLASMA_DEN;
	ions2.name = "Xe++";

	/*ions species Xe (neutrals)*/
	Species ions3(n_nodes);
	ions3.charge = -2e-22 ;		/*Approximation with neutron electrical charge*/
	ions3.mass = 131 * AMU;
	ions3.spwt = 2e3;			/*GV: the specific weight is adapted according to our needs for the results*/
	ions3.den0 = NEUT_DEN;
	ions3.name = "Xe";

	/*Variables for evaluation functions*/
	bool steady_state = false;
	double last_mass = 0;	//mass at the prior time step
	double last_mom = 0;	//momentum at the prior time step
	double last_en = 0;		//energy at the prior time step

	/*main loop*/
	int ts;
	for (ts = 0; ts < 500; ts++)
	{
		/*sample new particles*/
		InjectIons(&ions, volume, solver, dt);
		InjectIons(&ions2, volume, solver, dt);
		InjectIons(&ions3, volume, solver, dt);

		/*perform collisions, simulates collisions with neutrals*/
		/*GV: Implementation of DSMC MEX collision-> momentum exchange, inspired from Chapter 4 of
		Plasma Simulation by Examples*/
		DSMC_MEC_collision(&ions3, volume, dt);

		/*update velocity and move particles*/
		MoveParticles(&ions, volume, solver, dt);
		MoveParticles(&ions2, volume, solver, dt);
		MoveParticles(&ions3, volume, solver, dt);

		/*check values*/
		double max_den = 0;
		double max_den2 = 0;
		double max_den3 = 0;
		for (int n = 0; n < n_nodes; n++) if (ions.den[n] > max_den) max_den = ions.den[n];
		for (int n = 0; n < n_nodes; n++) if (ions2.den[n] > max_den2) max_den2 = ions2.den[n];
		for (int n = 0; n < n_nodes; n++) if (ions3.den[n] > max_den3) max_den3 = ions3.den[n];

		/*call potential solver*/
		solver.computePhi(ions.den);
		solver.computePhi(ions2.den);
		solver.computePhi(ions3.den);

		solver.updateEf();

		/*Evaluate steady state*/
		/*GV: check the steady state for all criterias, mass, momentum and energy for all species*/
		/*Initialization*/
		double tot_mass = ions.particles.size()*ions.mass + ions2.particles.size() * ions2.mass+ ions3.particles.size() * ions3.mass; /*Total mass of particles*/
		/*Total momentum of particles*/
		double tot_mom = getMomentum(&ions, ions.particles)+ getMomentum(&ions2, ions2.particles)+ getMomentum(&ions3, ions3.particles); 
		/*Compute Total Potential Energy*/
		double tot_en = getPE(ions.particles, volume, solver)+ getPE(ions2.particles, volume, solver)+ getPE(ions3.particles, volume, solver);

		/*compute new values to last*/
		const double tol = 1e-2;
		double MASS = abs((tot_mass - last_mass) / tot_mass);
		double MOMENTUM = abs((tot_mom - last_mom) / tot_mom);
		double ENERGY = abs((tot_en - last_en) / tot_en);
		if (MASS < tol && MOMENTUM < tol) {
				steady_state = true;
				cout << "Steady state reached at time step " << ts << endl;
		}

		/*update prior values*/
		last_mass = tot_mass;
		last_mom = tot_mom;
		last_en = tot_en;

		if ((ts + 1) % 10 == 0) OutputMesh(ts, &ions, volume, solver.uh, solver.ef, ions.den);
		if ((ts + 1) % 10 == 0) OutputMesh(ts, &ions2, volume, solver.uh, solver.ef, ions2.den);
		if ((ts + 1) % 10 == 0) OutputMesh(ts, &ions3, volume, solver.uh, solver.ef, ions3.den);

		/*show some diagnostics*/
		/*GV: Compute the Debye length*/
		double lambda_d = sqrt(EPS0 * solver.kTe / (max_den * QE));
		

		
		cout <<ts<<";"<<ions.particles.size()<<";"<< ions2.particles.size() << ";" << ions3.particles.size() << ";" <<
			max_den << ";" << max_den2 << ";" << max_den3 << ";" << MASS << ";" << MOMENTUM << ";" << ENERGY << ";" << lambda_d << 
			ComputerL(&ions, ions.particles)<<";"<< ComputerL(&ions2, ions2.particles)<<";"<< ComputerL(&ions3, ions3.particles)<< endl;
			
		
		

	}
		/*GV: write the results in a text file*/
	/*	ofstream outdata;
		outdata.open("results/data.dat");
		if (!outdata.is_open()) { cerr << "Failed to open file " << endl; exit(-1); }
		outdata << "ts: " << " " << "\t Name:" << " " << "\t Number of Particles:" << endl; 
		for (ts = 0; ts < 200; ts++)
		{
			outdata << "ts: " << ts << "\t Name: " << ions.name << "\t np: " << ions.particles.size() << endl;
			outdata << "ts: " << ts << "\t Name: " << ions2.name << "\t np: " << ions2.particles.size() <<  endl;
			outdata << "ts: " << ts << "\t Name: " << ions3.name << "\t np: " << ions3.particles.size() <<  endl;
		}
		outdata.close();*/




	/*output mesh*/
	OutputMesh(ts, &ions, volume, solver.uh, solver.ef, ions.den);
	OutputMesh(ts, &ions2, volume, solver.uh, solver.ef, ions2.den);
	OutputMesh(ts, &ions3, volume, solver.uh, solver.ef, ions3.den);

	/*output particles*/
	OutputParticles(&ions, ions.particles);
	OutputParticles(&ions2, ions2.particles);
	OutputParticles(&ions3, ions3.particles);

	/* grab starting time*/
	cout << "Simulation took " << getWallTime() << " seconds" << endl;

	return 0;
}

/*returns elapsed wall time in seconds*/
double getWallTime() {
	auto time_now = chrono::high_resolution_clock::now();
	chrono::duration<double> time_delta = time_now - time_start;
	return time_delta.count();
}


/*loads and initializes volume mesh*/
bool LoadVolumeMesh(const string file_name, Volume& volume)
{
	/*open file*/
	ifstream in(file_name);
	if (!in.is_open()) { cerr << "Failed to open " << file_name << endl; return false; }

	/*read number of nodes and elements*/
	int n_nodes, n_elements;
	in >> n_nodes >> n_elements;
	cout << "Mesh contains " << n_nodes << " nodes and " << n_elements << " elements" << endl;

	/*read the nodes*/
	for (int n = 0; n < n_nodes; n++)
	{
		int index;
		double x, y, z;

		in >> index >> x >> y >> z;
		if (index != n + 1) cout << "Inconsistent node numbering" << endl;

		volume.nodes.emplace_back(x / 1000., y / 1000., z / 1000.);
	}

	/*read elements, this will also contain edges and triangles*/
	for (int e = 0; e < n_elements; e++)
	{
		int index, type;
		int n1, n2, n3, n4;

		in >> index >> type;

		if (type != 304) { string s; getline(in, s); continue; }

		in >> n1 >> n2 >> n3 >> n4;

		/*flipping nodes 2 & 3 to get positive volumes*/
		volume.elements.emplace_back(n1 - 1, n2 - 1, n3 - 1, n4 - 1);
	}

	/*reset number of nodes and elements since we skipped bunch of lines and triangles*/
	n_nodes = volume.nodes.size();
	n_elements = volume.elements.size();

	/*compute element volumes*/
	for (Tetra& tet : volume.elements)
	{
		double M[4][4];

		/*set first column to 1*/
		for (int i = 0; i < 4; i++) M[i][0] = 1;

		/*loop over vertices*/
		for (int v = 0; v < 4; v++)
		{
			for (int dim = 0; dim < 3; dim++)
			{
				M[0][dim + 1] = volume.nodes[tet.con[0]].pos[dim];
				M[1][dim + 1] = volume.nodes[tet.con[1]].pos[dim];
				M[2][dim + 1] = volume.nodes[tet.con[2]].pos[dim];
				M[3][dim + 1] = volume.nodes[tet.con[3]].pos[dim];
			}
		}

		/*volume is (1/6)*det4(M)*/
		tet.volume = (1.0 / 6.0) * det4(M);

		/*flip ABCD to ADBC if negative volume*/
		if (tet.volume < 0) { int t = tet.con[1]; tet.con[1] = tet.con[3]; tet.con[3] = t; tet.volume = -tet.volume; }
	}

	/*precompute 3x3 determinants for LC computation*/
	for (Tetra& tet : volume.elements)
	{
		double M[3][3];
		/*loop over vertices*/
		for (int v = 0; v < 4; v++)
		{
			int v2, v3, v4;

			switch (v)
			{
			case 0: v2 = 1; v3 = 2; v4 = 3; break;
			case 1: v2 = 3; v3 = 2; v4 = 0; break;
			case 2: v2 = 3; v3 = 0; v4 = 1; break;
			case 3: v2 = 1; v3 = 0; v4 = 2; break;
			}

			double* p2 = volume.nodes[tet.con[v2]].pos;
			double* p3 = volume.nodes[tet.con[v3]].pos;
			double* p4 = volume.nodes[tet.con[v4]].pos;

			/*alpha*/
			M[0][0] = p2[0];
			M[0][1] = p2[1];
			M[0][2] = p2[2];
			M[1][0] = p3[0];
			M[1][1] = p3[1];
			M[1][2] = p3[2];
			M[2][0] = p4[0];
			M[2][1] = p4[1];
			M[2][2] = p4[2];
			tet.alpha[v] = det3(M);

			/*beta*/
			M[0][0] = 1;
			M[0][1] = p2[1];
			M[0][2] = p2[2];
			M[1][0] = 1;
			M[1][1] = p3[1];
			M[1][2] = p3[2];
			M[2][0] = 1;
			M[2][1] = p4[1];
			M[2][2] = p4[2];
			tet.beta[v] = det3(M);

			/*gamma*/
			M[0][0] = 1;
			M[0][1] = p2[0];
			M[0][2] = p2[2];
			M[1][0] = 1;
			M[1][1] = p3[0];
			M[1][2] = p3[2];
			M[2][0] = 1;
			M[2][1] = p4[0];
			M[2][2] = p4[2];
			tet.gamma[v] = det3(M);

			/*delta*/
			M[0][0] = 1;
			M[0][1] = p2[0];
			M[0][2] = p2[1];
			M[1][0] = 1;
			M[1][1] = p3[0];
			M[1][2] = p3[1];
			M[2][0] = 1;
			M[2][1] = p4[0];
			M[2][2] = p4[1];
			tet.delta[v] = det3(M);
		}
	}

	/*build cell connectivity, there is probably a faster way*/
	cout << "Building cell connectivity" << endl;

	/*reset connectivities*/
	for (int l = 0; l < n_elements; l++)
	{
		Tetra& tet = volume.elements[l];
		for (int v = 0; v < 4; v++) tet.cell_con[v] = -1;	/*no neighbor*/
	}


	for (int l = 0; l < n_elements; l++)
	{
		Tetra& tet = volume.elements[l];
		int v1, v2, v3;
		for (int v = 0; v < 4; v++)
		{
			/*skip if already set*/
			if (tet.cell_con[v] >= 0) continue;

			switch (v)
			{
			case 0: v1 = 1; v2 = 2; v3 = 3; break;
			case 1: v1 = 2; v2 = 3; v3 = 0; break;
			case 2: v1 = 3; v2 = 0; v3 = 1; break;
			case 3: v1 = 0; v2 = 1; v3 = 2; break;
			}

			/*loop over the tets again looking for one with these three vertices*/
			for (int m = l + 1; m < n_elements; m++)
			{
				Tetra& other = volume.elements[m];

				bool matches[4] = { false,false,false,false };
				int count = 0;
				for (int k = 0; k < 4; k++)
				{
					if (other.con[k] == tet.con[v1] ||
						other.con[k] == tet.con[v2] ||
						other.con[k] == tet.con[v3]) {
						count++; matches[k] = true;
					}
				}

				/*if three vertices match*/
				if (count == 3)
				{
					tet.cell_con[v] = m;

					/*set the cell connectivity for the index without a matching vertex to l*/
					for (int k = 0; k < 4; k++)
						if (!matches[k]) other.cell_con[k] = l;
				}
			}
		}
	}

	/*also compute node volumes by scattering cell volumes,this can only be done after 3x3 dets are computed*/

	/*first set all to zero*/
	for (Node& node : volume.nodes) { node.volume = 0; }

	for (int i = 0; i < n_elements; i++)
	{
		Particle dummy_part;
		Tetra& tet = volume.elements[i];
		dummy_part.cell_index = i;
		/*compute centroid position*/
		for (int dim = 0; dim < 3; dim++)
		{
			dummy_part.pos[dim] = 0;
			for (int v = 0; v < 4; v++) dummy_part.pos[dim] += 0.25 * volume.nodes[tet.con[v]].pos[dim];
		}

		bool found = XtoLtet(dummy_part, volume, false);
		if (!found) cout << "something is wrong" << endl;

		for (int v = 0; v < 4; v++)
		{
			volume.nodes[tet.con[v]].volume += dummy_part.lc[v] * tet.volume;
		}

	}

	/*mark nodes on open faces as open*/
	for (size_t e = 0; e < volume.elements.size(); e++)
	{
		Tetra& tet = volume.elements[e];
		for (int v = 0; v < 4; v++)
			if (tet.cell_con[v] < 0)	/*no neighbor*/
			{
				for (int i = 0; i < 4; i++)
				{
					if (i != v) volume.nodes[tet.con[i]].type = OPEN;
				}
			}
	}

	cout << " Done loading " << file_name << endl;
	return true;
}

/*loads nodes from a surface mesh file and sets them to the specified node type*/
bool LoadSurfaceMesh(const string file_name, Volume& volume, NodeType node_type)
{
	/*open file*/
	ifstream in(file_name);
	if (!in.is_open()) { cerr << "Failed to open " << file_name << endl; return false; }

	/*read number of nodes and elements*/
	int n_nodes, n_elements;
	in >> n_nodes >> n_elements;
	cout << "Mesh contains " << n_nodes << " nodes and " << n_elements << " elements" << endl;

	int nn = volume.nodes.size();

	/*read the nodes*/
	for (int n = 0; n < n_nodes; n++)
	{
		int index;
		double x, y, z;

		in >> index >> x >> y >> z;

		if (index<1 || index>nn) { cerr << "Incorrect node number " << index << endl; continue; }
		volume.nodes[index - 1].type = node_type;
	}

	cout << " Done loading " << file_name << endl;
	return true;
}



/*** FUNCTIONS ***/

/*saves volume mesh*/
void OutputMesh(int ts, Species *species, Volume& volume, double* phi, double** ef, double* ion_den)
{
	stringstream ss;
	ss << "results/mesh_" << species->name << setfill('0') << setw(4) << ts + 1 << ".vtu";
	ofstream out(ss.str());
	if (!out.is_open()) { cerr << "Failed to open file " << ss.str() << endl; exit(-1); }

	/*header*/
	out << "<?xml version=\"1.0\"?>\n";
	out << "<VTKFile type=\"UnstructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">\n";
	out << "<UnstructuredGrid>\n";
	out << "<Piece NumberOfPoints=\"" << volume.nodes.size() << "\" NumberOfVerts=\"0\" NumberOfLines=\"0\" ";
	out << "NumberOfStrips=\"0\" NumberOfCells=\"" << volume.elements.size() << "\">\n";

	/*points*/
	out << "<Points>\n";
	out << "<DataArray type=\"Float32\" NumberOfComponents=\"3\" format=\"ascii\">\n";
	for (Node& node : volume.nodes)
		out << node.pos[0] << " " << node.pos[1] << " " << node.pos[2] << "\n";
	out << "</DataArray>\n";
	out << "</Points>\n";

	/*Cells*/
	out << "<Cells>\n";
	out << "<DataArray type=\"Int32\" Name=\"connectivity\" format=\"ascii\">\n";
	for (Tetra& tetra : volume.elements)
		out << tetra.con[0] << " " << tetra.con[1] << " " << tetra.con[2] << " " << tetra.con[3] << "\n";
	out << "</DataArray>\n";

	out << "<DataArray type=\"Int32\" Name=\"offsets\" format=\"ascii\">\n";
	for (size_t e = 0; e < volume.elements.size(); e++)
		out << (e + 1) * 4 << " ";
	out << "\n";
	out << "</DataArray>\n";

	out << "<DataArray type=\"UInt8\" Name=\"types\" format=\"ascii\">\n";
	for (size_t e = 0; e < volume.elements.size(); e++)
		out << "10 ";
	out << "\n";
	out << "</DataArray>\n";
	out << "</Cells>\n";

	/*save point data*/
	out << "<PointData Scalars=\"phi\">\n";
	out << "<DataArray type=\"Int32\" Name=\"node_index\" format=\"ascii\">\n";
	for (size_t n = 0; n < volume.nodes.size(); n++)
		out << n << " ";
	out << "\n";
	out << "</DataArray>\n";
	out << "<DataArray type=\"Int32\" Name=\"node_type\" format=\"ascii\">\n";
	for (size_t n = 0; n < volume.nodes.size(); n++)
		out << volume.nodes[n].type << " ";
	out << "\n";
	out << "</DataArray>\n";
	out << "<DataArray type=\"Float32\" Name=\"phi." << species->name << "\" format=\"ascii\">\n"; /*GV: write the potential for all species*/
	for (size_t n = 0; n < volume.nodes.size(); n++)
		out << phi[n] << " ";
	out << "\n";
	out << "</DataArray>\n";

	out << "<DataArray type=\"Float32\" Name=\"ion_den." << species->name << "\" format=\"ascii\">\n"; /*GV: write the density for all species*/
	for (size_t n = 0; n < volume.nodes.size(); n++)
		out << ion_den[n] << " ";
	out << "\n";
	out << "</DataArray>\n";

	out << "</PointData>\n";

	/*save cell data*/
	out << "<CellData Vectors=\"ef\">\n";
	out << "<DataArray type=\"Float32\" NumberOfComponents=\"3\" Name=\"ef\" format=\"ascii\">\n";
	for (size_t e = 0; e < volume.elements.size(); e++)
		out << ef[e][0] << " " << ef[e][1] << " " << ef[e][2] << " ";
	out << "\n";
	out << "</DataArray>\n";

	out << "<DataArray type=\"Float32\" Name=\"cell_volume\" format=\"ascii\">\n";
	for (Tetra& tet : volume.elements)
		out << tet.volume << " ";
	out << "\n";
	out << "</DataArray>\n";

	out << "</CellData>\n";

	out << "</Piece>\n";
	out << "</UnstructuredGrid>\n";
	out << "</VTKFile>\n";

	out.close();
}


/*saves particle data*/
void OutputParticles(Species *species, vector<Particle>& particles)
{
	stringstream ss;
	ss << "results/particles_" << species->name << ".vtp";
	ofstream out(ss.str());
	if (!out.is_open()) { cerr << "Failed to open output file " << endl; exit(-1); }


	/*header*/
	out << "<?xml version=\"1.0\"?>\n";
	out << "<VTKFile type=\"PolyData\" version=\"0.1\" byte_order=\"LittleEndian\">\n";
	out << "<PolyData>\n";
	out << "<Piece NumberOfPoints=\"" << particles.size() << "\" NumberOfVerts=\"0\" NumberOfLines=\"0\" ";
	out << "NumberOfStrips=\"0\" NumberOfCells=\"0\">\n";

	/*points*/
	out << "<Points>\n";
	out << "<DataArray type=\"Float32\" NumberOfComponents=\"3\" format=\"ascii\">\n";
	for (Particle& part : particles)
		out << part.pos[0] << " " << part.pos[1] << " " << part.pos[2] << "\n";
	out << "</DataArray>\n";
	out << "</Points>\n";

	/*points*/
	out << "<Velocities>\n";
	out << "<DataArray type=\"Float32\" NumberOfComponents=\"3\" format=\"ascii\">\n";
	for (Particle& part : particles)
		out << part.vel[0] << " " << part.vel[1] << " " << part.vel[2] << "\n"; /*GV: adding the velocity for all the particles*/
	out << "</DataArray>\n";
	out << "</Velocities>\n";

	out << "</Piece>\n";
	out << "</PolyData>\n";
	out << "</VTKFile>\n";

	out.close();
}





/*computes determinant of a 4x4 matrix*/
double det4(double(*M)[4])
{
	double M0[3][3];
	double M1[3][3];
	double M2[3][3];
	double M3[3][3];

	for (int i = 0; i < 3; i++)
	{
		M0[i][0] = M[i + 1][1];
		M0[i][1] = M[i + 1][2];
		M0[i][2] = M[i + 1][3];

		M1[i][0] = M[i + 1][0];
		M1[i][1] = M[i + 1][2];
		M1[i][2] = M[i + 1][3];

		M2[i][0] = M[i + 1][0];
		M2[i][1] = M[i + 1][1];
		M2[i][2] = M[i + 1][3];

		M3[i][0] = M[i + 1][0];
		M3[i][1] = M[i + 1][1];
		M3[i][2] = M[i + 1][2];
	}

	return M[0][0] * det3(M0) -
		M[0][1] * det3(M1) +
		M[0][2] * det3(M2) -
		M[0][3] * det3(M3);
}

/*computes determinant of a 3x3 matrix*/
double det3(double(*M)[3])
{
	return M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1]) -
		M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0]) +
		M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0]);
}

namespace vec {
	void unit(double r[3], double v1[3])
	{
		double v_mag = sqrt(v1[0] * v1[0] + v1[1] * v1[1] + v1[2] * v1[2]);

		for (int i = 0; i < 3; i++) r[i] = v1[i] / v_mag;
	}

	void add(double r[3], double v1[3], double v2[3])
	{
		for (int i = 0; i < 3; i++) r[i] = v1[i] + v2[i];
	}

	void sub(double r[3], double v1[3], double v2[3])
	{
		for (int i = 0; i < 3; i++) r[i] = v1[i] - v2[i];
	}

	double dot(double v1[3], double v2[3])
	{
		double r = 0;
		for (int i = 0; i < 3; i++) r += v1[i] * v2[i];
		return r;
	}

	void cross(double r[3], double v1[3], double v2[3])
	{
		r[0] = v1[1] * v2[2] - v1[2] * v2[1];
		r[1] = -v1[0] * v2[2] + v1[2] * v2[0];
		r[2] = v1[0] * v2[1] - v1[1] * v2[0];
	}
}


/*** Samples particle on the inlet surface
here we just sample on a known plane. A generic code should instead sample
from the surface triangles making up the inlet face*/
void InjectIons(Species *species, Volume& volume, FESolver& solver, double dt)
{
	/*set area of the k=0 face, this should be the sum of triangle areas on the inlet*/
	/*GV: define the injection surface for all the particles according to the mesh geometry*/
	double area = PI*(pow(0.2,2)-pow(0.14,2));   /*computed with SALOME*/

	/*number of real ions per sec, given prescribed density and velocity*/
	double num_per_sec = species->den0 * ION_VELOCITY * area; 

	/*number of ions to generate in this time step*/
	double num_real = num_per_sec * dt;

	/*fraction number of macroparticles*/
	double fnum_mp = num_real / species->spwt + species->rem;

	/*integer number of macroparticles*/
	int num_mp = (int)fnum_mp;

	/*update reminder*/
	species->rem = fnum_mp - num_mp;

	/*sample particles*/
	for (int p = 0; p < num_mp; p++)
	{
		/*new particle*/
		Particle part;

		/*sample random position on the inlet face*/
		/*GV: Randomization of particles injection on the inlet face*/
			double a = rnd() * 360;
			double dist = sqrt(rnd() *(pow(0.18, 2) - pow(0.15,2)) + pow(0.15,2));
			part.pos[0] = 0;
			part.pos[1] = dist * cos(a);
			part.pos[2] = dist * sin(a);
			

		/*injecting cold beam*/
		/*GV: the velocity of injected particles varies between 0.2% and 100% of inlet ion velocity*/
			part.vel[0] = rnd2() * ION_VELOCITY;
			part.vel[1] = 0;
			part.vel[2] = 0;

		/*reverse if going in wrong direction*/
		if (part.vel[0] < 0) part.vel[0] = -part.vel[0];

		/*set initial tetrahedron*/
		for (part.cell_index = 0; part.cell_index < (int)volume.elements.size(); part.cell_index++)
		{
			bool inside = XtoLtet(part, volume, false);
			if (!inside) continue;	/*go to next tetrahedron*/
			else break; /*break out once we found the tet*/
		}

		/*sanity check, should not happen*/
		if (part.cell_index >= (int)volume.elements.size())
		{
			cerr <<"Failed to find initial element " <<part.cell_index<<" "<<volume.elements.size()<< endl;
			continue;
		}

		/*rewind velocity*/
		double ef_part[3];
		solver.evalEf(ef_part, part.cell_index, part.lc);

		for (int i = 0; i < 3; i++)
			part.vel[i] -= species->charge / species->mass * ef_part[i] * (0.5 * dt);

		/*add to list*/
		species->particles.push_back(part);
	}
}




/*returns true if particle is inside the magnetic chamber*/
/*GV: define according to mesh geometry if the particles leave 
 the domain of the magnetic chamber or not*/
bool Inside(double r[3])
{
	double cylinder_x0[3];
	cylinder_x0[0] = 0.1;
	cylinder_x0[1] = 0;
	cylinder_x0[2] = 0;

	double dx = fabs(r[0] - cylinder_x0[0]);
	double dy = fabs(r[1] - cylinder_x0[1]);
	double dz = fabs(r[2] - cylinder_x0[2]);

	if (dx <= 0.4 && dy <= 0.2 && dz <= 0.2) return true;
	return false;
}


/*updates ion velocities and positions*/
void MoveParticles(Species *species, Volume& volume, FESolver& solver, double dt)
{
	/* GV: Simple particle integrator to push particle with Boris Method
 * in particle in cell plasma simulation codes
 *
 * Derivation and overview is located at:
 * http://www.particleincell.com/2011/vxb-rotation/
 *
 * Written by Lubos Brieda, lubos.brieda@particleincell.com
 *
 */

	int n_elements = volume.elements.size();
	int n_nodes = (int)volume.nodes.size();

	/*Intermediate Variables for compute drift velocity*/
	double v_minus[3];
	double v_prime[3];
	double v_plus[3];

	double t[3];
	double s[3];
	double t_mag2;
	double B[3]; /*Component of Magnetic Field*/
	B[0] = 0.02; /*GV: Magnetic flux density of 0.02 T*/
	B[1] = 0;
	B[2] = 0;			

	/*reset ion density*/
	for (int i = 0; i < n_nodes; i++) species->den[i] = 0;

	/*move particles*/
	auto part_it = species->particles.begin();
	while (part_it != species->particles.end())
	{
		Particle& part = *part_it;

		/*update particle velocity*/
		double ef_part[3];
		solver.evalEf(ef_part, part.cell_index, part.lc);

		if (species->charge > 0)
		{

			for (int i = 0; i < 3; i++)
			{
				part.vel[i] += species->charge / species->mass * ef_part[i] * dt;

				/*t vector*/
				t[i] = (species->charge / species->mass) * B[i] * 0.5 * dt;

				/*magnitude of t, squared*/
				t_mag2 = t[0] * t[0] + t[1] * t[1] + t[2] * t[2];

				/*s vector*/
				s[i] = 2 * t[i] / (1 + t_mag2);

				/*v minus*/
				v_minus[i] = part.vel[i] + (species->charge / species->mass) * ef_part[i] * 0.5 * dt;
			}
			/*v prime*/
			double v_minus_cross_t[3];
			/*Cross product between v_minus and t*/
			vec::cross(v_minus_cross_t, v_minus, t);
			for (int i = 0; i < 3; i++) { v_prime[i] = v_minus[i] + v_minus_cross_t[i]; }

			/*v plus*/
			double v_prime_cross_s[3];
			/*Cross product between v_prime and s*/
			vec::cross(v_prime_cross_s, v_prime, s);


			for (int i = 0; i < 3; i++)
			{
				v_plus[i] = v_minus[i] + v_prime_cross_s[i];

				/*v n+1/2*/
				part.vel[i] = v_plus[i] + (species->charge / species->mass) * ef_part[i] * 0.5 * dt;
			}
		}
		else
		{
			for (int i = 0; i < 3; i++)
				part.vel[i] += species->charge / species->mass * ef_part[i] * dt;
		}
		
		double x_old[3];		/*position before the following move*/
		for (int i = 0; i < 3; i++) x_old[i] = part.pos[i];

		/*update particle positions from v=dx/dt*/
		for (int i = 0; i < 3; i++) part.pos[i] += part.vel[i] * dt;

		bool inside = XtoLtet(part, volume);

		bool alive = true; /*Says if the particle is ionized or not*/

		double cylinder_x0[3]; /*Defines point of reference */
		cylinder_x0[0] = 0.1;
		cylinder_x0[1] = 0;
		cylinder_x0[2] = 0;

		if (inside)
		{
			Tetra& tet = volume.elements[part.cell_index];
			/*now we know that we are inside this tetrahedron, scatter*/
			double sum = 0;
			for (int v = 0; v < 4; v++)
			{
				species->den[tet.con[v]] += part.lc[v];
				sum += part.lc[v];	/*for testing*/
			}

			/*testing*/
			if (abs(sum - 1.0) > 0.001) cout << sum << endl;

			part_it++;
		}
		else
		{	/*Diffusive reflection*/
			/*GV: adding diffusive reflection for neutral particles*/
			for (int n = 0; n < n_nodes; n++)
			{
				/*The neutral particules particles will be reflected*/
				if (species->charge = -2e-22) alive = true; 
				{
					/*diffuse reflection*/

					/*approximate intersection point, move particle there*/
					for (int i = 0; i < 3; i++)
						part.pos[i] = 0.5 * (x_old[i] + part.pos[i]);

					double cylinder_x0[3]; /*Defines point of reference */
					cylinder_x0[0] = 0.1;
					cylinder_x0[1] = 0;
					cylinder_x0[2] = 0;

					/*compute normal and two tangents*/
					double r[3], norm[3];
					vec::sub(r, part.pos, cylinder_x0);

					vec::unit(norm, r);

					/*two perpendicular vectors,
					http://math.stackexchange.com/questions/137362/how-to-find-perpendicular-vector-to-another-vector*/
					double perp1[3] = { norm[1],-norm[0],0 };
					double perp2[3] = { -norm[2],0,norm[0] };
					double tang1[3];

					vec::add(tang1, perp1, perp2);
					vec::unit(tang1, tang1);

					/*final tang given by cross product*/
					double tang2[3];
					vec::cross(tang2, norm, tang1);
					vec::unit(tang2, tang2);

					/*sanity check*/
					/*
					double dot1 = vec_dot(norm,tang1);
					double dot2 = vec_dot(norm,tang2);
					double dot3 = vec_dot(tang2,tang1);
					*/

					/*sample velocity from cosine distribution*/
					double sin_theta = rnd();
					double cos_theta = sqrt(1 - sin_theta * sin_theta);

					//random in plane angle
					double psi = rnd() * 2 * PI;

					//three vector components
					double a = sin_theta * cos(psi);
					double b = sin_theta * sin(psi);
					double c = cos_theta;

					//multiply by corresponding directions
					double v1[3];
					double v2[3];
					double v3[3];

					for (int i = 0; i < 3; i++)
					{
						v1[i] = tang1[i] * a;
						v2[i] = tang2[i] * b;
						v3[i] = norm[i] * c;
					}

					/*get initial velocity magnitude*/
					double v_mag = sqrt(part.vel[0] * part.vel[0] +
						part.vel[1] * part.vel[1] + part.vel[2] * part.vel[2]);

					/*add velocity components*/
					for (int i = 0; i < 3; i++) part.vel[i] = v_mag * (v1[i] + v2[i] + v3[i]);
				}
			}
		}
		/*GV:The charged particules stay on the walls and then will be deleted once they touched the walls*/
		if (!alive)
			part_it = species->particles.erase(part_it);	/*outside the mesh*/
	}
 /*convert to ion density*/
 for (int n = 0; n < n_nodes; n++) species->den[n] *= species->spwt / volume.nodes[n].volume;
}

/*converts physical coordinate to logical
Returns true if particle matched to a tet
*/
bool XtoLtet(Particle& part, Volume& volume, bool search)
{
	/*first try the current tetrahedron*/
	Tetra& tet = volume.elements[part.cell_index];

	bool inside = true;
	/*loop over vertices*/
	for (int i = 0; i < 4; i++)
	{
		part.lc[i] = (1.0 / 6.0) * (tet.alpha[i] - part.pos[0] * tet.beta[i] +
			part.pos[1] * tet.gamma[i] - part.pos[2] * tet.delta[i]) / tet.volume;
		if (part.lc[i] < 0 || part.lc[i]>1.0) inside = false;
	}

	if (inside) return true;

	if (!search) return false;
	/*we are outside the last known tet, find most negative weight*/
	int min_i = 0;
	double min_lc = part.lc[0];
	for (int i = 1; i < 4; i++)
		if (part.lc[i] < min_lc) { min_lc = part.lc[i]; min_i = i; }

	/*is there a neighbor in this direction?*/
	if (tet.cell_con[min_i] >= 0)
	{
		part.cell_index = tet.cell_con[min_i];
		return XtoLtet(part, volume);
	}

	return false;
}

/*Collision between two particles : ions-neutral*/
/*Inspired from chapter 4, Plasma Simulations with examples*/
void DSMC_MEX_collide(double &vel1, double &vel2, double mass1, double mass2)
{
	Particle part1;
	Particle part2;
	for (int i = 0; i < 3; i++)
	{
			double cm[3]; cm[i] = (mass1 * part1.vel[i] + mass2 * part2.vel[i]) / (mass1 + mass2);
			
			/*relative velocity, magnitude remains constant through the collision*/
			double cr[3]; cr[i] = part1.vel[i] - part2.vel[i];
			double cr_mag; cr_mag = sqrt(cr[0] * cr[0] + cr[1] * cr[1] + cr[2] * cr[2]);
			
			/*pick two random angles, per Bird's VHS method*/
			double cos_chi = 2 * rnd() - 1;
			double sin_chi = sqrt(1 - cos_chi * cos_chi);
			double eps = 2 * PI * rnd();

			/*perform rotation*/
			cr[0] = cr_mag * cos_chi;
			cr[1] = cr_mag * sin_chi * cos(eps);
			cr[2] = cr_mag * sin_chi * sin(eps);

			/*post collision velocities*/
			
			part1.vel[i] = cm[i] + mass2 / (mass1 + mass2) * cr[i];
			part2.vel[i] = cm[i] - mass1 / (mass1 + mass2) * cr[i];
	}
}

/*Estimate the number of collisions*/
void DSMC_MEC_collision(Species *species, Volume& volume, double dt)
{
	/*first we need to sort particles to cells*/
	vector<Particle*>* parts_in_cell; //array of vectors
	int n_elements = (int)volume.elements.size();
	parts_in_cell = new vector<Particle*>[n_elements];

	/*sort particles to cells*/
		for (Particle& part : species->particles)
		{
			int c = part.cell_index;
			parts_in_cell[c].push_back(&part);
		}

	double sigma_cr_max_temp = 0;	/*reset for max computation*/
	double sigma_cr_max = 1e-18;	//some initial value
	double dV = 0;	/*internal cell volume*/
	/*GV: compute element volumes from fem-pic code */
	for (Tetra& tet : volume.elements)
	{
		double M[4][4];

		/*set first column to 1*/
		for (int i = 0; i < 4; i++) M[i][0] = 1;

		/*loop over vertices*/
		for (int v = 0; v < 4; v++)
		{
			for (int dim = 0; dim < 3; dim++)
			{
				M[0][dim + 1] = volume.nodes[tet.con[0]].pos[dim];
				M[1][dim + 1] = volume.nodes[tet.con[1]].pos[dim];
				M[2][dim + 1] = volume.nodes[tet.con[2]].pos[dim];
				M[3][dim + 1] = volume.nodes[tet.con[3]].pos[dim];
			}
		}

		/*volume is (1/6)*det4(M)*/
		dV = (1.0 / 6.0) * det4(M);

		/*flip ABCD to ADBC if negative volume*/
		if (dV < 0) { int t = tet.con[1]; tet.con[1] = tet.con[3]; tet.con[3] = t; dV = -dV; }
	}
	double Fn = species->spwt;	/*specific weight, using Bird's notation*/
	int num_cols = 0;	/*reset collision counter*/
	/*now perform collisions*/
		for (int c = 0; c < n_elements; c++)
		{
			vector<Particle*>& parts = parts_in_cell[c];
			int np = parts.size();
			if (np < 2) continue;


			/*compute number of groups according to NTC*/
			double ng_f = 0.5 * np * np * Fn * sigma_cr_max * dt / dV;
			int ng = (int)(ng_f + 0.5);	/*number of groups, round*/

			/*assumes at least two particles per cell*/
			for (int g = 0; g < ng; g++)
			{
				int p1, p2;
				p1 = (int)(rnd() * np);		/*returns some number between 0 and np-1 inclusive*/

				do {
					p2 = (int)(rnd() * np);
				} while (p2 == p1);

				/*compute relative velocity*/
				for (int i = 0; i < 3; i++) {
					double cr_vec[3];  cr_vec[i] = parts[p1]->vel[i] - parts[p2]->vel[i];
					double cr = sqrt(cr_vec[0] * cr_vec[0] + cr_vec[1] * cr_vec[1] + cr_vec[2] * cr_vec[2]);

					/*evaluate cross section*/

					double mr = species->mass * species->mass / (species->mass + species->mass);
					double c[4];
					c[0] = 4.07e-10;
					c[1] = 0.77;
					c[2] = 2 * Kb * 316.0922 / mr;	//Bird's reference params at 316.0922 K
					c[3] = tgamma(2.5 - c[1]); //Gamma(5/2-w)
					double sigma = PI * c[0] * c[0] * pow(c[2] / (cr * cr), c[1] - 0.5) / c[3];

					/*eval sigma_cr*/
					double sigma_cr = sigma * cr;

					/*update sigma_cr_max*/
					if (sigma_cr > sigma_cr_max_temp)
						sigma_cr_max_temp = sigma_cr;

					/*eval prob*/
					double P = sigma_cr / sigma_cr_max;

					/*did the collision occur?*/
					if (P > rnd())
					{
						num_cols++;
						DSMC_MEX_collide(parts[p1]->vel[i], parts[p2]->vel[i], species->mass, species->mass);
						
					}
				}
			}

		}
		delete[] parts_in_cell;

		if (num_cols) {
			sigma_cr_max = sigma_cr_max_temp;
		}
}

/*Evaluation functions: this part can evualuate the performance of our thruster*/

/*computes species kinetic energy in electron volt*/
double ComputeKE(Species *species, vector<Particle>& particles)
{
	double ke = 0;
	
	for (Particle& part : particles)
	{
		/*update velocity and move particles*/	
		double v2 = part.vel[0] * part.vel[0] + part.vel[1] * part.vel[1] + part.vel[2] * part.vel[2];
		ke += species->spwt * v2;		
	}
	return (0.5 * species->mass * ke);
}

/*computes total momentum*/
double getMomentum(Species* species, vector<Particle>& particles)
{
	double momentum = 0;

	for (Particle& part : particles)
	{/*update velocity and move particles*/
		double v_mag = sqrt(part.vel[0] * part.vel[0] +
		part.vel[1] * part.vel[1] + part.vel[2] * part.vel[2]);
		momentum += v_mag * species->mass * species->spwt;
	}
return momentum;
}


/* computes total potential energy from 0.5*eps0*sum(E^2)*/
double getPE(vector<Particle>& particles, Volume& volume, FESolver& solver) 
{
	int n_nodes = volume.nodes.size();	
	double pe = 0;
	for (int n = 0; n < n_nodes; n++)
	{
		for (Particle& part : particles)
		{
			/*update particle velocity*/
			double ef_part[3];
			solver.evalEf(ef_part, part.cell_index, part.lc);
			
			double ef2 = ef_part[0] * ef_part[0] + ef_part[1] * ef_part[1] + ef_part[2] * ef_part[2];

			pe += ef2* volume.nodes[n].volume;
		}
	}
	return 0.5 * EPS0 * pe;
}

/*Compute Larmor Radius for B=Bx=0.01T*/
double ComputerL(Species* species, vector<Particle>& particles)
{
		double rL = 0; /*Initialization*/
		double B = 0.02; /*Magnetic flux density fixed at 0.02T*/
		for (Particle& part : particles)
		{/*update velocity and move particles*/
			rL += species->mass * part.vel[0] / (fabs(species->charge) * B);
		}
		return rL;
}