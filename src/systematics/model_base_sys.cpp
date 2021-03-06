#include "../../include/systematics/model_base_sys.h"

NuFit::model_base_sys::model_base_sys(std::vector<hists> fitdata, astro_model_base *astro, std::map<std::string, NuFit::interpolated_par *> systematics, unsigned int seed) : model_base(fitdata, astro), rand(seed)
{
        par_names.clear();

	// do not change order
        std::string par0("muon_norm");
        std::string par1("conv_norm");
        std::string par2("prompt_norm");
	std::string par3("muon_norm_mlb");

        par_names.push_back(par0);
        par_names.push_back(par1);
        par_names.push_back(par2);
	par_names.push_back(par3);

	// here goes systematics that involve weights
	// e.g. pion/kaon ratio and delta CR
	
	npars_sys_weights = 1;
	std::string par4("delta_cr");
	par_names.push_back(par4);

	pivot_energy_delta_cr_conv = 1883.86; // GeV
	pivot_energy_delta_cr_prompt = 5138.38; // GeV
	pivot_energy_delta_cr_muon = 2223.83; // GeV
 	
	// DO NOT CHANGE CODE BELOW
	// now add systematics parameters	
	for (std::map<std::string, NuFit::interpolated_par *>::iterator it=systematics.begin(); it!=systematics.end(); ++it)
	{	
		par_names.push_back(it->first); // to comply with model_base
		par_names_sys_bineff.push_back(it->first);
		std::cout << "... added interpolated systematics parameter: " << it->first << std::endl;
		// hold systematics in unordered map for faster lookup
		systematics_interp.insert(std::pair<std::string, NuFit::interpolated_par *>(it->first, it->second));
	}
	std::cout << std::endl;
      
        store_parameters();
	npars_sys_interp = systematics_interp.size();
	npars_sys = npars_sys_weights + npars_sys_interp;

	// DO NOT CHANGE BELOW
	// this vectors will be looped over to request correction factors from interpolated_par::parameter
	// interpolated_par::parameter handels internally whether a correction factor will be returned for flavor, component combination. 
	// if interpolated_par::parameter does not effect requested combo it will simply return 1.0
	// need to define flavors
	
	flavors.push_back(std::string("NuE"));
	flavors.push_back(std::string("NuMu"));
	flavors.push_back(std::string("NuTau"));

	// need to define components
	components.push_back(std::string("Conv"));
	components.push_back(std::string("Prompt"));
	components.push_back(std::string("Astro"));

	// assign prior values
	// first decide whether priors should be added
	add_prior_penalty = true;
	//add_prior_penalty = false;

	prior_domeff_mean_default = 0.99;
	prior_domeff_sigma = 0.1;
	prior_domeff_mean_current = prior_domeff_mean_default;

        prior_deltacr_mean_default = 0.0;
        prior_deltacr_sigma = 0.05;
        prior_deltacr_mean_current = prior_deltacr_mean_default;

	prior_scattering_default = 1.0;
	prior_scattering_current = prior_scattering_default;

	prior_absorption_default = 1.0;
	prior_absorption_current = prior_absorption_default;

        prior_muon_norm_mlb_mean_default = 1.0;
        prior_muon_norm_mlb_sigma = 0.38;
        prior_muon_norm_mlb_mean_current = prior_muon_norm_mlb_mean_default;


	// test random number generation
	/*
	for (unsigned int i=0; i<100; ++i) {
		double reff = generate_truncated_normal_rv(prior_domeff_mean_default, prior_domeff_sigma);
		double rcr = generate_normal_rv(prior_deltacr_mean_default, prior_deltacr_sigma);
		double means[2] = {1.0, 1.0};
		double rvars[2] = {0.0, 0.0};
		generate_truncated_bivnorm_rv(means, rvars);
		std::cout << "deff: " << reff << " dcr: " << rcr << " abs: " << rvars[0] << " scat: " << rvars[1] << std::endl;

	}
	*/


	skip = std::string("place_holder"); // no systematics treatment for specified selection

        return;
}

NuFit::model_base_sys::~model_base_sys() 
{ 
	for (std::unordered_map<std::string, NuFit::interpolated_par *>::iterator it=systematics_interp.begin(); it!=systematics_interp.end(); )
	{
		// clean up memory
		systematics_interp.erase(it++);
	}
	std::cout << std::endl;
	std::cout << std::endl;
        std::cout << "... cleaned model base with systematics class from memory." << std::endl;

}	

double NuFit::model_base_sys::likelihood(const double *pars)
{
	double llh = NuFit::model_base::likelihood(pars);
	if(add_prior_penalty) llh+=logprior(pars);

	return llh;
}

double NuFit::model_base_sys::logprior(const double *pars)
{
        double neglogl = 0.0;

	// muon_norm mlb
    	neglogl += gaussian_prior_penalty(pars[parameters["muon_norm_mlb"]], prior_muon_norm_mlb_mean_current, prior_muon_norm_mlb_sigma); 

        // domeff 
        neglogl += gaussian_prior_penalty(pars[parameters["dom_efficiency"]], prior_domeff_mean_current, prior_domeff_sigma);

        // scattering, absorption 
        neglogl += bivariate_prior_penalty(pars[parameters["absorption"]], pars[parameters["scattering"]], prior_absorption_current, prior_scattering_current);

        // delta cr 
        neglogl += gaussian_prior_penalty(pars[parameters["delta_cr"]], prior_deltacr_mean_current, prior_deltacr_sigma);
        
	return neglogl;
}


void NuFit::model_base_sys::update_bincorrections(const double *pars)
{
	// loop over all analyses
	for (unsigned int i=0; i<ndatasets; ++i) 
	{
		hists &dataset = input[i];

		if (dataset.name.compare(skip)==0) // do not apply to skip
			continue;

		// loop over all histogram bins
		for (unsigned int k=0; k<dataset.get_nbinsx(); ++k) 
		{
			for (unsigned int l=0; l<dataset.get_nbinsy(); ++l)
			{
				for (unsigned int m=0; m<dataset.get_nbinsz(); ++m)
				{
					// need to loop over all flavors
					for(unsigned int n=0; n<flavors.size(); ++n)
					{	
						std::string flavor = flavors[n]; 	
						// need to loop over all components
						for(unsigned int p=0; p<components.size(); ++p)
						{
							std::string component = components[p];						
							double efficiency_correction = 1.0;
							// need to loop over all systematics parameters to get correction factors	
							for (std::unordered_map<std::string, NuFit::interpolated_par *>::const_iterator it = systematics_interp.begin(); it != systematics_interp.end(); ++it)
							{
								// find parameter index
								unsigned int par_index = parameters[it->first];  
								double par_value = pars[par_index];	
								// get efficiency factor does not use ROOT convention.
								// k, l, m give correction factor for ROOT histogram bin
								// k+1, l+1, m+1	
								efficiency_correction *= (it->second)->get_efficiency_correction(par_value, dataset.name, flavor, component, k, l, m);	
							}
							// apply efficiency correction to bin	
							double bin_content = dataset.get_bincontent(flavor, component, k+1, l+1, m+1);	
							dataset.set_bincontent(flavor, component, k+1, l+1, m+1, bin_content * efficiency_correction);			
						}
					
					}

					// need to deal with muons here
					// e.g. check if muons are actually effected by this systematic
					// and then use sperate function to compute efficiency correction
					// e.g. get_efficiency_correction_muon(..)
					// or map efficiency correction fo another component to muons ...
					double efficiency_correction = 1.0;
					// loop through parameters 
					for (std::unordered_map<std::string, NuFit::interpolated_par *>::const_iterator it = systematics_interp.begin(); it != systematics_interp.end(); ++it)
					{
						// find parameter index
						unsigned int par_index = parameters[it->first];
						double par_value = pars[par_index];
						efficiency_correction *= (it->second)->get_efficiency_correction_muon(par_value, dataset.name, k, l, m);
					}
                                        double bin_content = dataset.get_bincontent_muon(k+1, l+1, m+1);
	                                dataset.set_bincontent_muon(k+1, l+1, m+1, bin_content * efficiency_correction);					
				}	
			}
		}
	}
	return;
}

void NuFit::model_base_sys::update_hists(const double *pars)
{
        double astro_pars[npars_astro];
        for (unsigned int i=0; i<npars_astro; ++i) {
                //std::cout << pars[i] << std::endl;
                astro_pars[i] = pars[npars_base+i];
        }

	double muon_norm = pars[0];
	double conv_norm = pars[1];
	double prompt_norm = pars[2];
	double muon_norm_mlb = pars[3];

	double delta_cr = pars[4];

	// adjust histograms according to parameter values
	// this needs to be performed on each dataset that enters the likelihood
	for (unsigned int i=0; i<ndatasets; ++i) 
	{

                hists &dataset = input[i];

                // first reset all histograms
                dataset.nue.astro.Reset();
		dataset.nue.conv.Reset();
		dataset.nue.prompt.Reset();

                dataset.numu.astro.Reset();
		dataset.numu.conv.Reset();
		dataset.numu.prompt.Reset();

                dataset.nutau.astro.Reset();
		dataset.nutau.conv.Reset();
		dataset.nutau.prompt.Reset();

		dataset.muon.hist.Reset();

                // adjust all histograms
		
		// nue
		for (unsigned int j=0; j<dataset.nue.get_size(); ++j)
		{
			dataset.nue.astro.Fill(dataset.nue.logenergy_rec[j], dataset.nue.coszenith_rec[j], dataset.nue.ra_rec[j], dataset.nue.astro_weight[j]*astro_model->get_flux(astro_pars, dataset.nue.energy_prim[j], dataset.nue.coszenith_prim[j], dataset.nue.ra_prim[j], dataset.nue.ptype[j]));
			//dataset.nue.conv.Fill(dataset.nue.logenergy_rec[j], dataset.nue.coszenith_rec[j], dataset.nue.ra_rec[j], dataset.nue.conv_weight[j]);
			//dataset.nue.prompt.Fill(dataset.nue.logenergy_rec[j], dataset.nue.coszenith_rec[j], dataset.nue.ra_rec[j], dataset.nue.prompt_weight[j]);
			dataset.nue.conv.Fill(dataset.nue.logenergy_rec[j], dataset.nue.coszenith_rec[j], dataset.nue.ra_rec[j], dataset.nue.conv_weight[j] * TMath::Power(dataset.nue.energy_prim[j]/pivot_energy_delta_cr_conv, -1.0*delta_cr));
			dataset.nue.prompt.Fill(dataset.nue.logenergy_rec[j], dataset.nue.coszenith_rec[j], dataset.nue.ra_rec[j], dataset.nue.prompt_weight[j] * TMath::Power(dataset.nue.energy_prim[j]/pivot_energy_delta_cr_prompt, -1.0* delta_cr));
		}
		
		dataset.nue.conv.Scale(conv_norm);
		dataset.nue.prompt.Scale(prompt_norm);

		// numu
		for (unsigned int j=0; j<dataset.numu.get_size(); ++j)
		{
			dataset.numu.astro.Fill(dataset.numu.logenergy_rec[j], dataset.numu.coszenith_rec[j], dataset.numu.ra_rec[j], dataset.numu.astro_weight[j]*astro_model->get_flux(astro_pars, dataset.numu.energy_prim[j], dataset.numu.coszenith_prim[j], dataset.numu.ra_prim[j], dataset.numu.ptype[j]));
			//dataset.numu.conv.Fill(dataset.numu.logenergy_rec[j], dataset.numu.coszenith_rec[j], dataset.numu.ra_rec[j], dataset.numu.conv_weight[j]);
			//dataset.numu.prompt.Fill(dataset.numu.logenergy_rec[j], dataset.numu.coszenith_rec[j], dataset.numu.ra_rec[j], dataset.numu.prompt_weight[j]);
			dataset.numu.conv.Fill(dataset.numu.logenergy_rec[j], dataset.numu.coszenith_rec[j], dataset.numu.ra_rec[j], dataset.numu.conv_weight[j] * TMath::Power(dataset.numu.energy_prim[j]/pivot_energy_delta_cr_conv, -1.0*delta_cr));
			dataset.numu.prompt.Fill(dataset.numu.logenergy_rec[j], dataset.numu.coszenith_rec[j], dataset.numu.ra_rec[j], dataset.numu.prompt_weight[j] * TMath::Power(dataset.numu.energy_prim[j]/pivot_energy_delta_cr_prompt, -1.0*delta_cr));
		}
		

		dataset.numu.conv.Scale(conv_norm);
		dataset.numu.prompt.Scale(prompt_norm);

		// nutau (no atmospheric contribution)
		for (unsigned int j=0; j<dataset.nutau.get_size(); ++j)
			dataset.nutau.astro.Fill(dataset.nutau.logenergy_rec[j], dataset.nutau.coszenith_rec[j], dataset.nutau.ra_rec[j], dataset.nutau.astro_weight[j]*astro_model->get_flux(astro_pars, dataset.nutau.energy_prim[j], dataset.nutau.coszenith_prim[j], dataset.nutau.ra_prim[j], dataset.nutau.ptype[j]));

		// muon
		if (dataset.name.compare(std::string("cascade_mlb"))==0) {
			for (unsigned int j=0; j<dataset.muon.get_size(); ++j)
				dataset.muon.hist.Fill(dataset.muon.logenergy_rec[j], dataset.muon.coszenith_rec[j], dataset.muon.ra_rec[j], dataset.muon.muon_weight[j]);
			dataset.muon.hist.Scale(muon_norm_mlb);
		}


		else {
			for (unsigned int j=0; j<dataset.muon.get_size(); ++j)
				//dataset.muon.hist.Fill(dataset.muon.logenergy_rec[j], dataset.muon.coszenith_rec[j], dataset.muon.ra_rec[j], dataset.muon.muon_weight[j]);
				dataset.muon.hist.Fill(dataset.muon.logenergy_rec[j], dataset.muon.coszenith_rec[j], dataset.muon.ra_rec[j], dataset.muon.muon_weight[j] * TMath::Power(dataset.muon.energy_prim[j]/pivot_energy_delta_cr_muon, -1.0*delta_cr));

			dataset.muon.hist.Scale(muon_norm);
		}

	}


        update_bincorrections(pars); // this is new compared to base class implementation: model_base.cpp
        update_sum();

}

void NuFit::model_base_sys::update_sum() 
{
	// sum components
	for (unsigned int i=0; i<ndatasets; ++i)
	{
		hists &dataset = input[i];
		dataset.astro.Reset();
                dataset.astro.Add(&dataset.nue.astro);
                dataset.astro.Add(&dataset.numu.astro);
                dataset.astro.Add(&dataset.nutau.astro);	

		dataset.atm_conv.Reset();
		dataset.atm_conv.Add(&dataset.nue.conv);
		dataset.atm_conv.Add(&dataset.numu.conv);

		dataset.atm_prompt.Reset();
		dataset.atm_prompt.Add(&dataset.nue.prompt);
		dataset.atm_prompt.Add(&dataset.numu.prompt);

		dataset.mcsum.Reset();
		dataset.mcsum.Add(&dataset.astro);
		dataset.mcsum.Add(&dataset.atm_conv);
		dataset.mcsum.Add(&dataset.atm_prompt);
		dataset.mcsum.Add(&dataset.muon.hist);

	}
	
}

double NuFit::model_base_sys::generate_normal_rv(const double &mean, const double &sigma) 
{
	// normal random variable
	std::normal_distribution<double> norm_rv(mean, sigma);
	return norm_rv(rand);
}

double NuFit::model_base_sys::generate_truncated_normal_rv(const double &mean, const double &sigma) 
{
	// 0-truncated normal random variable
	double rv = -1.0;
	while (rv<=0.0)
		rv = generate_normal_rv(mean, sigma);
	
	return rv;
}

void NuFit::model_base_sys::generate_truncated_bivnorm_rv(double means[2], double rvars[2])
{
	// 0-truncated bivariate normal random variable
        
	// Spice paper ellipse
	// double rho=-0.1;
	double sigma=0.07;
	
	double asin_rho = -0.1001674211615598; // asin(rho)

	rvars[0]=-1.0;
	rvars[1]=-1.0;

	while ((rvars[0]<=0.0)||(rvars[1]<=0.0)) {
	// Box Muller Transforms to get (0,0) centered RV
		std::uniform_real_distribution<double> unif(0.0, 1.0);
		double u1 = unif(rand);
		double u2 = unif(rand);
		
		double v1 = sigma * TMath::Sqrt(-2.0 * TMath::Log(u2)) * TMath::Cos(TMath::Pi() * 2.0 * u1);
		double v2 = sigma * TMath::Sqrt(-2.0 * TMath::Log(u2)) * TMath::Sin(TMath::Pi() * 2.0 * u1 + asin_rho);
		rvars[0] = v1 + means[0]; // move to mean
		rvars[1] = v2 + means[1];
	}
	
	return;
}

void NuFit::model_base_sys::update_auxillary_data(std::map<std::string, double> &point) {

        prior_domeff_mean_current = point["dom_efficiency"]; 
        prior_deltacr_mean_current = point["delta_cr"];
        prior_absorption_current = point["absorption"];
        prior_scattering_current = point["scattering"];
	prior_muon_norm_mlb_mean_current = point["muon_norm_mlb"];
        //std::cout << "dom_eff: " << prior_domeff_mean_current << " delta_cr: " << prior_deltacr_mean_current << " absorption: " << prior_absorption_current << " scattering: " << prior_scattering_current << " HI scattering: " << prior_hi_mean_current << std::endl;

}


void NuFit::model_base_sys::reset_auxillary_data()
{
	// reset to default constraints
	prior_domeff_mean_current = prior_domeff_mean_default;
	prior_deltacr_mean_current = prior_deltacr_mean_default;
	prior_absorption_current = prior_absorption_default;
	prior_scattering_current = prior_scattering_default;
	prior_muon_norm_mlb_mean_current = prior_muon_norm_mlb_mean_default;

	return;
}
