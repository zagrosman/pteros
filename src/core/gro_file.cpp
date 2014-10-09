/*
 *
 *                This source code is part of
 *                    ******************
 *                    ***   Pteros   ***
 *                    ******************
 *                 molecular modeling library
 *
 * Copyright (c) 2009-2013, Semen Yesylevskyy
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of Artistic License:
 *
 * Please note, that Artistic License is slightly more restrictive
 * then GPL license in terms of distributing the modified versions
 * of this software (they should be approved first).
 * Read http://www.opensource.org/licenses/artistic-license-2.0.php
 * for details. Such license fits scientific software better then
 * GPL because it prevents the distribution of bugged derivatives.
 *
*/

#include "gro_file.h"
#include "pteros/core/pteros_error.h"

using namespace std;
using namespace pteros;
using namespace Eigen;

GRO_file::GRO_file(string fname, char open_mode): Mol_file(fname, open_mode)
{
    if(open_mode=='r'){
        f.open(fname.c_str(),ios_base::in);
        if(!f) throw Pteros_error("Can't open GRO file '"+fname+"'' for reading");
    } else {
        f.open(fname.c_str(),ios_base::out);
        if(!f) throw Pteros_error("Can't open GRO file '"+fname+"'' for writing");
    }
}

GRO_file::~GRO_file(){
    if(f){
        f.close();
    }
}

bool GRO_file::do_read(System *sys, Frame *frame, const Mol_file_content &what){

    string line;

    int N,i,j;
    float v;
    // tmp atom
    Atom tmp_atom;
    // Temporary coordinates
    Vector3f tmp_coor;
    // Buffers for scanf
    char resname_buf[5], name_buf[5];

    // Skip header line
    getline(f,line);    

    // Read number of atoms
    getline(f,line);
    sscanf(line.c_str(),"%d",&N);

    frame->coord.resize(N);

    // Read coordinates
    for(i=0;i<N;++i){
        getline(f,line);

        sscanf(line.c_str(),"%5d%5c%5c%5d%8f%8f%8f",
                &tmp_atom.resid, resname_buf, name_buf, &j,
                &tmp_coor(0), &tmp_coor(1), &tmp_coor(2));

        tmp_atom.resname = resname_buf;
        tmp_atom.name = name_buf;

        // Coordinates are in nm, so no need to convert

        if(what.structure){
            // Assign masses
            tmp_atom.mass = get_mass_from_atom_name(tmp_atom.name);
            tmp_atom.type = -1; //Undefined type so far
            // There is no chain, occupancy and beta in GRO file, so add it manually
            tmp_atom.chain = 'X';
            tmp_atom.beta = 0.0;
            tmp_atom.occupancy = 0.0;
            // Add new atom to the system
            append_atom_in_system(*sys,tmp_atom);
        }

        if(what.coordinates){
            // Add column of coordinates
            frame->coord[i] = tmp_coor;
        }
    }

    if(what.coordinates){        
        // Read box. Adapted form VMD.
        stringstream ss;
        getline(f,line);
        ss.clear();
        ss.str(line);
        //ss >> &x[0], &y[1], &z[2], &x[1], &x[2], &y[0], &y[2], &z[0], &z[1])
        Matrix3f box;
        box.fill(0.0);
        ss >> box(0,0) >> box(1,1) >> box(2,2);
        // Try to read nex val. If failed we have rectangular box.
        ss >> v;
        if(ss.good()){
            box(0,1) = v;
            ss >> box(0,2) >> box(1,0) >> box(1,2)
               >> box(2,0) >> box(2,1);
        }
        // Box is in nm in gro files, no need to convert
        // Transpose the box because we want column-vectors (the code above uses row-vectors)
        box.transposeInPlace();

        frame->box.modify(box);
    }

    return true;
}

void GRO_file::do_write(const Selection &sel, const Mol_file_content &what){
    int n = sel.size();
    char ch[80];

    if(!(what.coordinates && what.structure))
        throw Pteros_error("It is impossible to write individual components to GRO file!");

    // Print title
    f << "Created by Pteros" << endl;
    // Write number of atoms
    f << n << endl;
    int ind;
    for(int i=0;i<n;i++){
        ind = (i%100000)+1; // Prevents overflow of index field. It's not used anyway.
        sprintf(ch,"%5d%-5s%5s%5d%8.3f%8.3f%8.3f",
                sel.Resid(i), sel.Resname(i).c_str(), sel.Name(i).c_str(), ind,
                sel.X(i), sel.Y(i), sel.Z(i));
        f << ch << endl;
    }

    // Write periodic box
    Eigen::Matrix3f b;
    if(sel.get_system()->Box(sel.get_frame()).is_periodic()){
        // We store box as column-vectors, while the code below hacked from VMD use row vectors,
        // so, transpose
        b = sel.get_system()->Box(sel.get_frame()).get_box().transpose();
    } else {
        b.fill(0.0);
    }
    // We are writing dimensions in nm to be compatible with Gromacs
    // Write diagonal anyway
    f << b(0,0) << " "
      << b(1,1) << " "
      << b(2,2);
    // Write off-diagonal only for triclinic boxes
    if(sel.get_system()->Box(sel.get_frame()).is_triclinic()){
        f << " "
          << b(0,1) << " "
          << b(0,2) << " "
          << b(1,0) << " "
          << b(1,2) << " "
          << b(2,0) << " "
          << b(2,1);
    }
    // Mandatory endline at the end of file!
    f << endl;
}
