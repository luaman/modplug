#pragma once

#include "libopenmpt_settings.h"

namespace libopenmpt {

	using namespace System;
	using namespace System::ComponentModel;
	using namespace System::Collections;
	using namespace System::Windows::Forms;
	using namespace System::Data;
	using namespace System::Drawing;

	/// <summary>
	/// Summary for SettingsForm
	/// </summary>
	public ref class SettingsForm : public System::Windows::Forms::Form
	{
	private:
		openmpt::settings * settings;
	public:
		SettingsForm( const char * title, openmpt::settings * s ) : settings(s)
		{
			InitializeComponent();

			Text = gcnew System::String( title );

			comboBoxSamplerate->Items->Add(6000);
			comboBoxSamplerate->Items->Add(8000);
			comboBoxSamplerate->Items->Add(11025);
			comboBoxSamplerate->Items->Add(16000);
			comboBoxSamplerate->Items->Add(22050);
			comboBoxSamplerate->Items->Add(32000);
			comboBoxSamplerate->Items->Add(44100);
			comboBoxSamplerate->Items->Add(48000);
			comboBoxSamplerate->Items->Add(88200);
			comboBoxSamplerate->Items->Add(96000);
			comboBoxSamplerate->SelectedItem = settings->samplerate;

			comboBoxChannels->Items->Add("mono");
			comboBoxChannels->Items->Add("stereo");
			comboBoxChannels->Items->Add("quad");
			if ( settings->channels == 1 ) comboBoxChannels->SelectedItem = "mono";
			if ( settings->channels == 2 ) comboBoxChannels->SelectedItem = "stereo";
			if ( settings->channels == 3 ) comboBoxChannels->SelectedItem = "quad";

			comboBoxSamplerate->Enabled = settings->with_outputformat;
			comboBoxChannels->Enabled = settings->with_outputformat;

			trackBarGain->Value = settings->mastergain;

			trackBarMaxPolyphony->Value = settings->maxmixchannels;

			comboBoxInterpolation->SelectedIndex = settings->interpolationmode - 1;

			comboBoxRepeat->SelectedIndex = settings->repeatcount + 1;

			trackBarStereoSeparation->Value = settings->stereoseparation;

			trackBarVolrampin->Value = settings->volrampinus;
			trackBarVolrampout->Value = settings->volrampoutus;

			//
			//TODO: Add the constructor code here
			//
		}

	protected:
		/// <summary>
		/// Clean up any resources being used.
		/// </summary>
		~SettingsForm()
		{
			if (components)
			{
				delete components;
			}
		}
	private: System::Windows::Forms::ComboBox^  comboBoxSamplerate;
	protected: 
	private: System::Windows::Forms::Label^  labelSamplerate;
	private: System::Windows::Forms::Button^  buttonOK;
	private: System::Windows::Forms::Label^  labelChannels;
	private: System::Windows::Forms::ComboBox^  comboBoxChannels;
	private: System::Windows::Forms::Label^  labelGain;
	private: System::Windows::Forms::TrackBar^  trackBarGain;
	private: System::Windows::Forms::Label^  labelMaxPolyphony;
	private: System::Windows::Forms::TrackBar^  trackBarMaxPolyphony;
	private: System::Windows::Forms::Label^  labelInterpolation;
	private: System::Windows::Forms::ComboBox^  comboBoxInterpolation;
	private: System::Windows::Forms::Label^  labelRepeat;
	private: System::Windows::Forms::ComboBox^  comboBoxRepeat;
	private: System::Windows::Forms::Label^  labelStereoSeparation;
	private: System::Windows::Forms::TrackBar^  trackBarStereoSeparation;
	private: System::Windows::Forms::Label^  labelVolrampin;
	private: System::Windows::Forms::Label^  labelVolrampout;
	private: System::Windows::Forms::TrackBar^  trackBarVolrampin;
	private: System::Windows::Forms::TrackBar^  trackBarVolrampout;


	protected: 

	private:
		/// <summary>
		/// Required designer variable.
		/// </summary>
		System::ComponentModel::Container ^components;

#pragma region Windows Form Designer generated code
		/// <summary>
		/// Required method for Designer support - do not modify
		/// the contents of this method with the code editor.
		/// </summary>
		void InitializeComponent(void)
		{
			this->comboBoxSamplerate = (gcnew System::Windows::Forms::ComboBox());
			this->labelSamplerate = (gcnew System::Windows::Forms::Label());
			this->buttonOK = (gcnew System::Windows::Forms::Button());
			this->labelChannels = (gcnew System::Windows::Forms::Label());
			this->comboBoxChannels = (gcnew System::Windows::Forms::ComboBox());
			this->labelGain = (gcnew System::Windows::Forms::Label());
			this->trackBarGain = (gcnew System::Windows::Forms::TrackBar());
			this->labelMaxPolyphony = (gcnew System::Windows::Forms::Label());
			this->trackBarMaxPolyphony = (gcnew System::Windows::Forms::TrackBar());
			this->labelInterpolation = (gcnew System::Windows::Forms::Label());
			this->comboBoxInterpolation = (gcnew System::Windows::Forms::ComboBox());
			this->labelRepeat = (gcnew System::Windows::Forms::Label());
			this->comboBoxRepeat = (gcnew System::Windows::Forms::ComboBox());
			this->labelStereoSeparation = (gcnew System::Windows::Forms::Label());
			this->trackBarStereoSeparation = (gcnew System::Windows::Forms::TrackBar());
			this->labelVolrampin = (gcnew System::Windows::Forms::Label());
			this->labelVolrampout = (gcnew System::Windows::Forms::Label());
			this->trackBarVolrampin = (gcnew System::Windows::Forms::TrackBar());
			this->trackBarVolrampout = (gcnew System::Windows::Forms::TrackBar());
			(cli::safe_cast<System::ComponentModel::ISupportInitialize^  >(this->trackBarGain))->BeginInit();
			(cli::safe_cast<System::ComponentModel::ISupportInitialize^  >(this->trackBarMaxPolyphony))->BeginInit();
			(cli::safe_cast<System::ComponentModel::ISupportInitialize^  >(this->trackBarStereoSeparation))->BeginInit();
			(cli::safe_cast<System::ComponentModel::ISupportInitialize^  >(this->trackBarVolrampin))->BeginInit();
			(cli::safe_cast<System::ComponentModel::ISupportInitialize^  >(this->trackBarVolrampout))->BeginInit();
			this->SuspendLayout();
			// 
			// comboBoxSamplerate
			// 
			this->comboBoxSamplerate->DropDownStyle = System::Windows::Forms::ComboBoxStyle::DropDownList;
			this->comboBoxSamplerate->FormattingEnabled = true;
			this->comboBoxSamplerate->Location = System::Drawing::Point(106, 9);
			this->comboBoxSamplerate->Name = L"comboBoxSamplerate";
			this->comboBoxSamplerate->Size = System::Drawing::Size(121, 21);
			this->comboBoxSamplerate->TabIndex = 1;
			this->comboBoxSamplerate->SelectedIndexChanged += gcnew System::EventHandler(this, &SettingsForm::comboBoxSamplerate_SelectedIndexChanged);
			// 
			// labelSamplerate
			// 
			this->labelSamplerate->AutoSize = true;
			this->labelSamplerate->Location = System::Drawing::Point(12, 12);
			this->labelSamplerate->Name = L"labelSamplerate";
			this->labelSamplerate->Size = System::Drawing::Size(58, 13);
			this->labelSamplerate->TabIndex = 0;
			this->labelSamplerate->Text = L"samplerate";
			// 
			// buttonOK
			// 
			this->buttonOK->Location = System::Drawing::Point(15, 357);
			this->buttonOK->Name = L"buttonOK";
			this->buttonOK->Size = System::Drawing::Size(212, 23);
			this->buttonOK->TabIndex = 2;
			this->buttonOK->Text = L"OK";
			this->buttonOK->UseVisualStyleBackColor = true;
			this->buttonOK->Click += gcnew System::EventHandler(this, &SettingsForm::buttonOK_Click);
			// 
			// labelChannels
			// 
			this->labelChannels->AutoSize = true;
			this->labelChannels->Location = System::Drawing::Point(12, 39);
			this->labelChannels->Name = L"labelChannels";
			this->labelChannels->Size = System::Drawing::Size(50, 13);
			this->labelChannels->TabIndex = 3;
			this->labelChannels->Text = L"channels";
			// 
			// comboBoxChannels
			// 
			this->comboBoxChannels->DropDownStyle = System::Windows::Forms::ComboBoxStyle::DropDownList;
			this->comboBoxChannels->FormattingEnabled = true;
			this->comboBoxChannels->Location = System::Drawing::Point(106, 36);
			this->comboBoxChannels->Name = L"comboBoxChannels";
			this->comboBoxChannels->Size = System::Drawing::Size(121, 21);
			this->comboBoxChannels->TabIndex = 4;
			// 
			// labelGain
			// 
			this->labelGain->AutoSize = true;
			this->labelGain->Location = System::Drawing::Point(12, 74);
			this->labelGain->Name = L"labelGain";
			this->labelGain->Size = System::Drawing::Size(27, 13);
			this->labelGain->TabIndex = 5;
			this->labelGain->Text = L"gain";
			// 
			// trackBarGain
			// 
			this->trackBarGain->LargeChange = 3;
			this->trackBarGain->Location = System::Drawing::Point(106, 63);
			this->trackBarGain->Maximum = 12;
			this->trackBarGain->Minimum = -12;
			this->trackBarGain->Name = L"trackBarGain";
			this->trackBarGain->Size = System::Drawing::Size(121, 42);
			this->trackBarGain->TabIndex = 6;
			this->trackBarGain->TickStyle = System::Windows::Forms::TickStyle::Both;
			this->trackBarGain->Scroll += gcnew System::EventHandler(this, &SettingsForm::trackBarGain_Scroll);
			// 
			// labelMaxPolyphony
			// 
			this->labelMaxPolyphony->AutoSize = true;
			this->labelMaxPolyphony->Location = System::Drawing::Point(12, 124);
			this->labelMaxPolyphony->Name = L"labelMaxPolyphony";
			this->labelMaxPolyphony->Size = System::Drawing::Size(77, 13);
			this->labelMaxPolyphony->TabIndex = 7;
			this->labelMaxPolyphony->Text = L"max polyphony";
			// 
			// trackBarMaxPolyphony
			// 
			this->trackBarMaxPolyphony->LargeChange = 16;
			this->trackBarMaxPolyphony->Location = System::Drawing::Point(106, 111);
			this->trackBarMaxPolyphony->Maximum = 256;
			this->trackBarMaxPolyphony->Minimum = 16;
			this->trackBarMaxPolyphony->Name = L"trackBarMaxPolyphony";
			this->trackBarMaxPolyphony->Size = System::Drawing::Size(121, 42);
			this->trackBarMaxPolyphony->TabIndex = 8;
			this->trackBarMaxPolyphony->TickFrequency = 16;
			this->trackBarMaxPolyphony->TickStyle = System::Windows::Forms::TickStyle::Both;
			this->trackBarMaxPolyphony->Value = 256;
			this->trackBarMaxPolyphony->Scroll += gcnew System::EventHandler(this, &SettingsForm::trackBarMaxPolyphony_Scroll);
			// 
			// labelInterpolation
			// 
			this->labelInterpolation->AutoSize = true;
			this->labelInterpolation->Location = System::Drawing::Point(12, 162);
			this->labelInterpolation->Name = L"labelInterpolation";
			this->labelInterpolation->Size = System::Drawing::Size(64, 13);
			this->labelInterpolation->TabIndex = 9;
			this->labelInterpolation->Text = L"interpolation";
			// 
			// comboBoxInterpolation
			// 
			this->comboBoxInterpolation->DropDownStyle = System::Windows::Forms::ComboBoxStyle::DropDownList;
			this->comboBoxInterpolation->FormattingEnabled = true;
			this->comboBoxInterpolation->Items->AddRange(gcnew cli::array< System::Object^  >(12) {L"nearest", L"linear", L"spline", L"polyphase", 
				L"fir hann", L"fir hamming", L"fir blackman exact", L"fir blackman 3 tap 1", L"fir blackman 3 tap 2", L"fir blackman 4 tap 1", 
				L"fir blackman 4 tap 2", L"fir kaiser 4 tap"});
			this->comboBoxInterpolation->Location = System::Drawing::Point(106, 159);
			this->comboBoxInterpolation->Name = L"comboBoxInterpolation";
			this->comboBoxInterpolation->Size = System::Drawing::Size(121, 21);
			this->comboBoxInterpolation->TabIndex = 10;
			this->comboBoxInterpolation->SelectedIndexChanged += gcnew System::EventHandler(this, &SettingsForm::comboBoxInterpolation_SelectedIndexChanged);
			// 
			// labelRepeat
			// 
			this->labelRepeat->AutoSize = true;
			this->labelRepeat->Location = System::Drawing::Point(12, 189);
			this->labelRepeat->Name = L"labelRepeat";
			this->labelRepeat->Size = System::Drawing::Size(37, 13);
			this->labelRepeat->TabIndex = 11;
			this->labelRepeat->Text = L"repeat";
			// 
			// comboBoxRepeat
			// 
			this->comboBoxRepeat->DropDownStyle = System::Windows::Forms::ComboBoxStyle::DropDownList;
			this->comboBoxRepeat->FormattingEnabled = true;
			this->comboBoxRepeat->Items->AddRange(gcnew cli::array< System::Object^  >(3) {L"forever", L"never", L"once"});
			this->comboBoxRepeat->Location = System::Drawing::Point(106, 186);
			this->comboBoxRepeat->Name = L"comboBoxRepeat";
			this->comboBoxRepeat->Size = System::Drawing::Size(121, 21);
			this->comboBoxRepeat->TabIndex = 12;
			this->comboBoxRepeat->SelectedIndexChanged += gcnew System::EventHandler(this, &SettingsForm::comboBoxRepeat_SelectedIndexChanged);
			// 
			// labelStereoSeparation
			// 
			this->labelStereoSeparation->AutoSize = true;
			this->labelStereoSeparation->Location = System::Drawing::Point(12, 226);
			this->labelStereoSeparation->Name = L"labelStereoSeparation";
			this->labelStereoSeparation->Size = System::Drawing::Size(88, 13);
			this->labelStereoSeparation->TabIndex = 13;
			this->labelStereoSeparation->Text = L"stereo separation";
			// 
			// trackBarStereoSeparation
			// 
			this->trackBarStereoSeparation->LargeChange = 100;
			this->trackBarStereoSeparation->Location = System::Drawing::Point(106, 213);
			this->trackBarStereoSeparation->Maximum = 400;
			this->trackBarStereoSeparation->Name = L"trackBarStereoSeparation";
			this->trackBarStereoSeparation->Size = System::Drawing::Size(121, 42);
			this->trackBarStereoSeparation->SmallChange = 25;
			this->trackBarStereoSeparation->TabIndex = 14;
			this->trackBarStereoSeparation->TickFrequency = 100;
			this->trackBarStereoSeparation->TickStyle = System::Windows::Forms::TickStyle::Both;
			this->trackBarStereoSeparation->Value = 100;
			this->trackBarStereoSeparation->Scroll += gcnew System::EventHandler(this, &SettingsForm::trackBarStereoSeparation_Scroll);
			// 
			// labelVolrampin
			// 
			this->labelVolrampin->AutoSize = true;
			this->labelVolrampin->Location = System::Drawing::Point(12, 274);
			this->labelVolrampin->Name = L"labelVolrampin";
			this->labelVolrampin->Size = System::Drawing::Size(78, 13);
			this->labelVolrampin->TabIndex = 15;
			this->labelVolrampin->Text = L"volume ramp in";
			// 
			// labelVolrampout
			// 
			this->labelVolrampout->AutoSize = true;
			this->labelVolrampout->Location = System::Drawing::Point(12, 321);
			this->labelVolrampout->Name = L"labelVolrampout";
			this->labelVolrampout->Size = System::Drawing::Size(85, 13);
			this->labelVolrampout->TabIndex = 16;
			this->labelVolrampout->Text = L"volume ramp out";
			// 
			// trackBarVolrampin
			// 
			this->trackBarVolrampin->LargeChange = 1000;
			this->trackBarVolrampin->Location = System::Drawing::Point(106, 261);
			this->trackBarVolrampin->Maximum = 10000;
			this->trackBarVolrampin->Name = L"trackBarVolrampin";
			this->trackBarVolrampin->Size = System::Drawing::Size(121, 42);
			this->trackBarVolrampin->TabIndex = 17;
			this->trackBarVolrampin->TickFrequency = 1000;
			this->trackBarVolrampin->TickStyle = System::Windows::Forms::TickStyle::Both;
			this->trackBarVolrampin->Value = 363;
			this->trackBarVolrampin->Scroll += gcnew System::EventHandler(this, &SettingsForm::trackBarVolrampin_Scroll);
			// 
			// trackBarVolrampout
			// 
			this->trackBarVolrampout->LargeChange = 1000;
			this->trackBarVolrampout->Location = System::Drawing::Point(106, 309);
			this->trackBarVolrampout->Maximum = 10000;
			this->trackBarVolrampout->Name = L"trackBarVolrampout";
			this->trackBarVolrampout->Size = System::Drawing::Size(121, 42);
			this->trackBarVolrampout->TabIndex = 18;
			this->trackBarVolrampout->TickFrequency = 1000;
			this->trackBarVolrampout->TickStyle = System::Windows::Forms::TickStyle::Both;
			this->trackBarVolrampout->Value = 952;
			this->trackBarVolrampout->Scroll += gcnew System::EventHandler(this, &SettingsForm::trackBarVolrampout_Scroll);
			// 
			// SettingsForm
			// 
			this->AutoScaleDimensions = System::Drawing::SizeF(6, 13);
			this->AutoScaleMode = System::Windows::Forms::AutoScaleMode::Font;
			this->AutoSize = true;
			this->AutoSizeMode = System::Windows::Forms::AutoSizeMode::GrowAndShrink;
			this->ClientSize = System::Drawing::Size(436, 477);
			this->Controls->Add(this->trackBarVolrampout);
			this->Controls->Add(this->trackBarVolrampin);
			this->Controls->Add(this->labelVolrampout);
			this->Controls->Add(this->labelVolrampin);
			this->Controls->Add(this->trackBarStereoSeparation);
			this->Controls->Add(this->labelStereoSeparation);
			this->Controls->Add(this->comboBoxRepeat);
			this->Controls->Add(this->labelRepeat);
			this->Controls->Add(this->comboBoxInterpolation);
			this->Controls->Add(this->labelInterpolation);
			this->Controls->Add(this->trackBarMaxPolyphony);
			this->Controls->Add(this->labelMaxPolyphony);
			this->Controls->Add(this->trackBarGain);
			this->Controls->Add(this->labelGain);
			this->Controls->Add(this->comboBoxChannels);
			this->Controls->Add(this->labelChannels);
			this->Controls->Add(this->buttonOK);
			this->Controls->Add(this->labelSamplerate);
			this->Controls->Add(this->comboBoxSamplerate);
			this->MaximizeBox = false;
			this->MinimizeBox = false;
			this->Name = L"SettingsForm";
			this->ShowIcon = false;
			this->ShowInTaskbar = false;
			this->SizeGripStyle = System::Windows::Forms::SizeGripStyle::Hide;
			this->StartPosition = System::Windows::Forms::FormStartPosition::CenterParent;
			this->Text = L"SettingsForm";
			(cli::safe_cast<System::ComponentModel::ISupportInitialize^  >(this->trackBarGain))->EndInit();
			(cli::safe_cast<System::ComponentModel::ISupportInitialize^  >(this->trackBarMaxPolyphony))->EndInit();
			(cli::safe_cast<System::ComponentModel::ISupportInitialize^  >(this->trackBarStereoSeparation))->EndInit();
			(cli::safe_cast<System::ComponentModel::ISupportInitialize^  >(this->trackBarVolrampin))->EndInit();
			(cli::safe_cast<System::ComponentModel::ISupportInitialize^  >(this->trackBarVolrampout))->EndInit();
			this->ResumeLayout(false);
			this->PerformLayout();

		}
#pragma endregion
	private: System::Void comboBoxSamplerate_SelectedIndexChanged(System::Object^  sender, System::EventArgs^  e) {
						 settings->samplerate = (int)comboBoxSamplerate->SelectedItem;
						 settings->changed();
					 }
	private: System::Void buttonOK_Click(System::Object^  sender, System::EventArgs^  e) {
						 this->Close();
						 settings->changed();
					 }
private: System::Void comboBoxChannels_SelectedIndexChanged(System::Object^  sender, System::EventArgs^  e) {
					 System::String ^ val = (System::String ^)comboBoxChannels->SelectedItem;
					 if ( val == "mono" ) settings->channels = 1;
					 if ( val == "stereo" ) settings->channels = 2;
					 if ( val == "quad" ) settings->channels = 4;
					 settings->changed();
				 }
private: System::Void trackBarGain_Scroll(System::Object^  sender, System::EventArgs^  e) {
					 settings->mastergain = (int)trackBarGain->Value;
					 settings->changed();
				 }
private: System::Void comboBoxInterpolation_SelectedIndexChanged(System::Object^  sender, System::EventArgs^  e) {
					 settings->interpolationmode = (int)comboBoxInterpolation->SelectedIndex + 1;
					 settings->changed();
				 }
private: System::Void trackBarMaxPolyphony_Scroll(System::Object^  sender, System::EventArgs^  e) {
					 settings->maxmixchannels = (int)trackBarMaxPolyphony->Value;
					 settings->changed();
				 }
private: System::Void comboBoxRepeat_SelectedIndexChanged(System::Object^  sender, System::EventArgs^  e) {
					 settings->repeatcount = (int)comboBoxRepeat->SelectedIndex - 1;
					 settings->changed();
				 }
private: System::Void trackBarStereoSeparation_Scroll(System::Object^  sender, System::EventArgs^  e) {
					settings->stereoseparation = (int)trackBarStereoSeparation->Value;
					settings->changed();
				}
private: System::Void trackBarVolrampin_Scroll(System::Object^  sender, System::EventArgs^  e) {
					 settings->volrampinus = (int)trackBarVolrampin->Value;
					 settings->changed();
				 }
private: System::Void trackBarVolrampout_Scroll(System::Object^  sender, System::EventArgs^  e) {
					 settings->volrampoutus = (int)trackBarVolrampout->Value;
					 settings->changed();
				 }
};
}