#include "pteros/pteros.h"
#include <Eigen/Core>
#include "spdlog/fmt/ostr.h"

using namespace std;
using namespace pteros;
using namespace Eigen;

string help(){
    return
R"(Usage:
-solute <file>  - structure file with solute
-solvent <file> - structure file with the box of solvent
    Defaults to spc216.gro from Gromacs dir if Gromacs is installed
    otherwise no default.
    Only rectangular solvent boxes are supported.
-d <float>, default: 0.25 - minimal distance from solute to solvent in nm
    measured between the centers of atoms.
-sel <string>, optional - custom selection of atoms to remove.
    Executed after cutoff=d was applied.
    Useful for removing water from within lipid bilayer or protein cavities.
    **NOTE**: Always use 'pbc' in within selection to get meaningful result!
    **NOTE**: This selection is used 'as is', so be careful not to remove
    your solute or doing other crazy things!
-o <file>, default 'solvated.pdb' - output file.
)";
}

int main(int argc, char* argv[]){
    try{
        cout << "===================================" << endl;
        cout << "==        pteros_solvate         ==" << endl;
        cout << "===================================" << endl;
        cout << "==  (C) Semen Yesylevskyy, 2018  ==" << endl;
        cout << "===================================" << endl;

        LOG()->set_pattern("(%l)\t%v");

        Options opt;
        parse_command_line(argc,argv,opt);

        if(opt.has("help")){
            cout << help();
            return 0;
        }

        // Solute

        string solute_file = opt("solute").as_string();
        LOG()->info("Loading solute from '{}'...", solute_file);
        System solute( solute_file );

        // Solvent

        System solvent;
        string solvent_file;
        // Look for $GMXDATA environmental variable
        if (const char* env_gmx = std::getenv("GMXDATA")) {
            solvent_file = opt("solvent",string(env_gmx)+"/top/spc216.gro").as_string();
        } else {
            solvent_file = opt("solvent").as_string();
        }

        LOG()->info("Loading solvent from '{}'...", solvent_file);
        solvent.load( solvent_file );

        if(solvent.box(0).is_triclinic())
            throw Pteros_error("Only rectangular solvent boxes are allowed!");

        // See how many solvent boxes should be used to cover solute

        // In arbitrary triclinic boxes find the maximal box coordinate
        Vector3f max_solute_coord = solute.box(0).box_to_lab( solute.box(0).extents() );
        Vector3f max_solvent_coord = solvent.box(0).extents();

        Vector3i nbox;
        for(int i=0; i<3; ++i) nbox(i) = int(ceil(max_solute_coord(i)/max_solvent_coord(i)));

        LOG()->info("Will use {} solvent boxes...", nbox.transpose());

        // Distribute solvent boxes
        {
            auto all = solvent.select_all();
            auto m = solvent.box(0).get_matrix();
            LOG()->info("Distributing solvent boxes...");
            solvent.distribute(all,nbox,m);
        }

        // Move min coords of solvent and solute to zero
        Vector3f solvent_min,solvent_max, solute_min, solute_max;
        auto solute_all = solute.select_all();
        auto solvent_all = solvent.select_all();
        solute_all.minmax(solute_min,solute_max);
        solvent_all.minmax(solvent_min,solvent_max);

        solvent_all.translate(-solvent_min);
        solute_all.translate(-solute_min);

        LOG()->info("Finding solvent atoms outside the solute box...");

        // Cut solvent atoms outside the solute box
        vector<int> bad;        
        for(int i=0; i<solvent_all.size(); ++i){            
            if( !solute.box(0).in_box(solvent_all.xyz(i)) ) bad.push_back(solvent_all.index(i));
        }

        // Select bad atoms
        Selection bad_sel(solvent,bad);
        // Select whole bad residues
        vector<Selection> bad_res;
        bad_sel.each_residue(bad_res);

        LOG()->info("Found {} solvent molecules outside the solute box...", bad_res.size());
        for(auto& sel: bad_res){
            sel.set_beta(-1000);
        }

        // Find last index of solute
        int last_solute_ind = solute.num_atoms()-1;

        // append good solvent to solute
        solute.append(solvent("beta > -1000"));        

        // select overlapping water
        float d = opt("d","0.25").as_float();
        string s = fmt::format("by residue within {} pbc noself of index 0-{}", d, last_solute_ind);

        Selection sel(solute, s);

        LOG()->info("Found {} overlaping solvent atoms at cutoff={}", sel.size(),d);

        // Remove overlapping water
        sel.set_beta(-1000);

        // If we have custom selection use it
        if(opt.has("sel")){            
            s = opt("sel").as_string();
            Selection sel(solute, s);
            LOG()->info("Removing atoms from custom selection '{}' ({} atoms})", s, sel.size());
            sel.set_beta(-1000);
        }

        // Translate back to initial box center
        solute_all.modify("beta > -1000");
        solute_all.translate(solute_min);

        // Report number of remaining solvent residues
        map<string,int> residues;
        int at=last_solute_ind+1;
        do {
            string resname = solute_all.resname(at);
            int resind = solute_all.resindex(at);

            // Find the end of this residue
            do {
                ++at;
            } while( at<solute_all.size() && solute_all.resindex(at) == resind);

            if(residues.count(resname)){
                // such resname is present
                ++residues[resname];
            } else {
                // new resname
                residues[resname] = 1;
            }

        } while(at<solute_all.size());

        LOG()->info("Number of solvent molecules added:");
        for(auto& it: residues){
            LOG()->info("\t{}: {}", it.first,it.second);
        }

        // Writing output
        auto out = opt("o","solvated.pdb").as_string();
        LOG()->info("Writing output to '{}'...", out);
        solute_all.write(out);

    } catch(const Pteros_error& e) {
        LOG()->error(e.what());
    }
}
