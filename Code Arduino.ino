//----------------------------------------------------------LIBRAIRIES---------------------------------------------------------------------//
#include <SerialFlash.h>
#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <iostream>
#include <vector>
#include <tuple>

//----------------------------------------------------------VARIABLES---------------------------------------------------------------------//

//Connexions audios
AudioInputI2S in;
AudioAnalyzeNoteFrequency notefreq;
AudioOutputI2S out;

AudioControlSGTL5000 audioShield;

AudioConnection patchCord1(in,0,out,0);//oreille droite
AudioConnection patchCord2(in,0,out,1);//oreille gauche

//PEAK // Utiliser une fft pour seulement trouver un peak est overkill
AudioAnalyzePeak peak;

//filtre
AudioConnection patchCord3(in, 0, notefreq, 0);
AudioConnection patchCord0(in,0,peak,0);

//Variables du timer
int startTime; // Déclaration d'une variable pour stocker le temps de début
int endTime; // Déclaration d'une variable pour stocker le temps de fin
int duree;

//Mémoire de fréquence et amplitude
float ancienAmplitude;
float ancienNote;

//Variables pendant l'algo
std::vector<float> monTableauFreq_moy;//tableau pour la fréq moy
std::vector<float> mesFrequences;//tableau des fréquences jouées
std::vector<float> mesDurees;

//Tableau noms fichiers CSV
std::vector<String> nomsFichiersCSV = {"Au clair de la lune", "Clair de lune Debussy", "Frère Jacques", "Pavane Faure"};
std::vector<String> DonneesCSV;
String fichier;

//----------------------------------------------------------FONCTIONS---------------------------------------------------------------------//

//Détecte si deux notes sont identiques, à une harmoniques près
int EstMemeNote(float ancienNote, float nouvelleNote){
  if(abs(ancienNote - nouvelleNote) <5){
    return 1;
  }
  for(int i = 2; i <= 4; i+=2){
    if((abs(ancienNote/float(i) - nouvelleNote)<5) || (abs(ancienNote*i - nouvelleNote)<5)){
      return 1;
    }
  }
  return 0;
}

//Associe la fréquence lue par le Teensy à une fréquence d'une note réelle la plus proche
float FrequenceExacteNote(float freqLue){
  float ecartPrecedent = 1000;
  float ecart = 0;
  float freq = 0;
  float freqPrecedent = 0;
  for(int i = 0; i<60; i++){
    freq = 130.81*pow(2, i/12.0); //Arrondi au dixième
    ecart = abs(freq - freqLue);
    if (ecartPrecedent < ecart){
      return freqPrecedent;
    }
    ecartPrecedent = ecart;
    freqPrecedent = freq;
  }
  return freq;
}

//Détermine la note qui est jouée en faisant une moyenne (pratique si une des harmoniques et détectée)
float CalculerNoteFinale(std::vector<float>& vec) {
  
    // Calculer la moyenne du vecteur initial
    float moyenne = 0.0;
    for (int elem : vec) {
        moyenne += elem;
    }
    moyenne /= vec.size();

    // Enlever les valeurs avec un écart trop grand par rapport à la moyenne
    auto it = vec.begin();
    while (it != vec.end()) {
        if (std::fabs(*it - moyenne) > 1/vec.size() *moyenne) {
            vec.erase(it);
        } else {
            ++it;
        }
    }

    moyenne = 0.0;
    //Calcul de la moyenne finale
    for (float elem : vec) {
        moyenne += elem;
    }
    moyenne /= vec.size();
    return FrequenceExacteNote(moyenne);
}

//Extraction des données du CSV
// Fonction pour diviser une chaîne en paires "freq;duree" en fonction d'un délimiteur
std::pair<std::vector<float>, std::vector<int>> ExtraireDonneesFichier(String& fichierString) {
    std::vector<float> tableauFreq;
    std::vector<int> tableauDuree;
    int lastPairDelimiterIndex = -1;

    // Parcourir la chaîne d'entrée
    for (int i = 0; i < int(fichierString.length()); i++) {
        // Si le caractère est le délimiteur de paire
        if (fichierString.charAt(i) == '|') {
            // Extraire la paire entre le dernier délimiteur de paire et le caractère actuel
            String pair = fichierString.substring(lastPairDelimiterIndex + 1, i);
            
            // Trouver l'index du délimiteur de sous-chaîne dans la paire
            int subDelimiterIndex = pair.indexOf(';');

            // Extraire la fréquence et la durée de la paire et les stocker dans les vecteurs de sortie
            tableauFreq.push_back(pair.substring(0, subDelimiterIndex).toFloat());
            tableauDuree.push_back(pair.substring(subDelimiterIndex + 1).toInt());

            // Passer au prochain délimiteur de paire
            lastPairDelimiterIndex = i;
        }
    }

    // Extraire la dernière paire après le dernier délimiteur de paire
    if (lastPairDelimiterIndex < int(fichierString.length()) - 1) {
        String lastPair = fichierString.substring(lastPairDelimiterIndex + 1);
        int lastSubDelimiterIndex = lastPair.indexOf(';');
        tableauFreq.push_back(lastPair.substring(0, lastSubDelimiterIndex).toFloat());
        tableauDuree.push_back(lastPair.substring(lastSubDelimiterIndex + 1).toInt());
    }

    // Renvoyer les vecteurs de fréquences et de durées
    return std::make_pair(tableauFreq, tableauDuree);
}

//Algorithme de comparaison
void ShazamMorceau(std::vector<float>& freq, std::vector<float>& duree){
  float scoreMax = 0;
  int indiceMax = 0;
  std::vector<float> frequences;
  std::vector<int> durees;

  for(int i = 0; i < int(nomsFichiersCSV.size()); i++){
    float scoreMorceau = 0.0, oubli = 0, rajout = 0; //Score du morceau / Nombre de corrects d'affilé / Nombre de notes rajoutés / Nombre de notes oubliés
    std::tie(frequences, durees) = ExtraireDonneesFichier(DonneesCSV[i]);
    int nbFrequencesAComparer = (frequences.size() < freq.size()) ? frequences.size() : freq.size();
    for(int k = 0; k < 12; k++){
      float score = 0.0;
      for(int j = 0; j < nbFrequencesAComparer; j++){
        if(freq[j] <= 150 || freq[j] == 209){
          break;
        }
        if (EstMemeNote(freq[j], FrequenceExacteNote(frequences[j]*pow(2, k/12.0)))){ //Potentiellement pas le bon octave
          score += 1;
        }
        //else if(EstMemeNote(freq[j+1+rajout+oubli],FrequenceExacteNote(frequences[j]*pow(2, k/12.0)))){ //Potentiellement pas le bon octave
          //rajout += 1;
          //score += 1;
        //}
        //else if(EstMemeNote(freq[j-1+rajout+oubli], FrequenceExacteNote(frequences[j]*pow(2, k/12.0)))){ //Potentiellement pas le bon octave
          //oubli -= 1;
          //score += 1;
        //}
      }
      //score = score - rajout + oubli;
      if(score > scoreMorceau){
        scoreMorceau = score;
      }
  }
    scoreMorceau = scoreMorceau/(nbFrequencesAComparer);
    if (scoreMorceau > scoreMax){
      scoreMax = scoreMorceau;
      indiceMax = i;
    }
  }
  Serial.print("Le morceau le plus ressemblant est : ");
  Serial.println(nomsFichiersCSV[indiceMax]);
  Serial.print("\n Avec le score de : ");
  Serial.println(scoreMax);
}

void afficherTableau(std::vector<float>& vec) {
    for (auto& element : vec) {
        Serial.printf("%.2f ", element);
    }
    Serial.printf("\n");
}

//----------------------------------------------------------SETUP---------------------------------------------------------------------//
void setup() {
  Serial.begin(9600);
  AudioMemory(27);
  notefreq.begin(.15);
  pinMode(0, INPUT); //Bouton

  audioShield.enable();
  audioShield.inputSelect(AUDIO_INPUT_MIC);
  audioShield.micGain(30); // in dB
  audioShield.volume(1);

}

//----------------------------------------------------------LOOP---------------------------------------------------------------------//
void loop() {
    std::vector<float> monTableauFreq;

    if (Serial.available()) {
        // Lire les données envoyées par Python
        String ligne = Serial.readStringUntil('\n');

        // Traiter les données en fonction du marqueur spécial
        if (ligne == "fin_du_fichier") {
            // Enregistrement
            DonneesCSV.push_back(fichier);
            fichier = "";
        } else {
            // Faire quelque chose avec les autres données
            if (ligne != ""){
              if (fichier != ""){
                fichier += "|" + ligne;
              }
              else{
                fichier = ligne;
              }
            } 
        }
    }
    
    if (digitalRead(0)) { // button is pressed
      
      if (notefreq.available()) {
        float note = notefreq.read();
        float ampli = peak.read()*100;
        
        if(note > 150.0){//filtrage du bruit de fond et voix

          //Enregistrer note dans tableau
          monTableauFreq_moy.push_back(note);

          if(ancienAmplitude == 0){
            ancienAmplitude = ampli;
          }

          note = CalculerNoteFinale(monTableauFreq_moy);

          //Timer 
          endTime = millis();
          if(startTime!=0){
              duree = endTime - startTime;
          }
          if(((abs(ampli - ancienAmplitude) < 2) && (int(duree) > 150)) || (EstMemeNote(note, ancienNote) == 0)){
            //Enregistrement notes + durees
            mesDurees.push_back(duree);
            mesFrequences.push_back(note);
            Serial.print("Duree Note : ");
            Serial.println(duree);
            Serial.print("<----------------------------------->\n");
            
            //Note / Amplitude
            Serial.printf("Note: %3.2f\n",note);
            Serial.print("Amplitude : ");
            Serial.println(ampli);
            Serial.print("\n");

            //Enregistrement Tampon note / amplitude / timer
            ancienAmplitude = ampli;
            ancienNote = note;
            startTime=endTime;
          }
      }
     
     }

    }
    else{//button not pressed
        
        if(mesFrequences.empty()){
          Serial.print("Le tableau est vide;");
          }
          else{
            mesDurees.erase(mesDurees.begin());//supprimer la première durée nulle;
            mesFrequences.pop_back();
            Serial.print("Mes frequences :");
            afficherTableau(mesFrequences);
            Serial.print("Mes durees :");
            afficherTableau(mesDurees);

            ShazamMorceau(mesFrequences, mesDurees);

            //cleear des données
            mesDurees.clear();
            mesFrequences.clear();

            }
      delay(500);
      }

}