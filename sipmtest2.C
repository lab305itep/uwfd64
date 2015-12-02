void sipmtest2(char *name)
{
	TFile *f;
	TTree *tr;
	TCanvas *cv;
	TH1D *h[64];
	char str[1024], strs[64];
	int i, j;
	const int pos[15] = {15, 10, 5, 14, 9, 4, 13, 8, 3, 12, 7, 2, 11, 6, 1};
	
	gStyle->SetOptStat(10);
	gStyle->SetTitleFontSize(0.1);
	gStyle->SetStatFontSize(0.15);
	gStyle->SetLabelSize(0.05);
	sprintf(str, "%s.root", name);
	f = new TFile(str);
	if (!f->IsOpen()) return;
	tr = (TTree *)f->Get("TD");
	if (!tr) return;
	
	cv = new TCanvas("CV", "Plots", 4000, 2500);
	
	for (i=0; i<4; i++) {
		cv->Clear();
		cv->Divide(5, 3);
		for (j=0; j<15; j++) {
			cv->cd(pos[j]);
			sprintf(strs, "s%d", 16*i + j);
			sprintf(str, "Chan:%d", 16*i + j);
			h[16*i+j] = new TH1D(strs, str, 100, 0, 100);
			sprintf(str, "chan == %d && type == 0", 16*i + j);
			tr->Project(strs, "A", str);
			h[16*i+j]->Draw();
		}
		switch(i) {
		case 0:
		    sprintf(str, "%s.pdf(", name);
		    break;
		default:
		    sprintf(str, "%s.pdf", name);
		    break;
		}
		cv->SaveAs(str);
	}
	for (i=0; i<4; i++) {
		cv->Clear();
		cv->Divide(5, 3);
		for (j=0; j<15; j++) {
			cv->cd(pos[j]);
			sprintf(strs, "c%d", 16*i + j);
			sprintf(str, "Chan:%d", 16*i + j);
			h[16*i+j] = new TH1D(strs, str, 68, 100, 3500);
			sprintf(str, "chan == %d && type == 1", 16*i + j);
			tr->Project(strs, "A", str);
			h[16*i+j]->Draw();
		}
		switch(i) {
		case 3:
		    sprintf(str, "%s.pdf)", name);
		    break;
		default:
		    sprintf(str, "%s.pdf", name);
		    break;
		}
		cv->SaveAs(str);
	}
	f->Close();
}
