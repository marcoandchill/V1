import serial
import time

nomsFichiersCSV = ["AuClairDeLaLune.csv", "ClairDeLuneDebussy.csv", "FrereJacques.csv", "PavaneFaure.csv"]

def ouvrirConnexion(comport, baudrate=9600):

    ser = serial.Serial(comport, baudrate, timeout=0.1) # 1/timeout is the frequency at which the port is read
    return ser

def transmettreDonnees(ser):

    for nom_fichier in nomsFichiersCSV:
        with open(nom_fichier, 'r') as fichier:
            for ligne in fichier:
                # Envoyer chaque ligne du fichier CSV à l'Arduino
                ser.write((ligne + "\n").encode())
            # Ajouter un marqueur spécial pour indiquer la fin des données de chaque fichier
            ser.write(b"fin_du_fichier\n")
            # Attendre un court délai pour s'assurer que l'Arduino a le temps de traiter les données
            time.sleep(0.1)

def readserial(ser, timestamp=False):

    mesFrequences = []
    mesDurees = []

    while True:

        data = ser.readline().decode().strip()
        
        if(data.startswith("Mes frequences :")):
            data=data.split(":")[1].strip()
            data=data.replace(" ",";")
            mesFrequences = data.split(";")
        
        if(data.startswith("Mes durees :")):
            data=data.split(":")[1].strip()
            data=data.replace(" ",";")
            mesDurees=data.split(";")

        if(mesDurees and mesFrequences):

            try:
                with open("maChanson.csv", 'w') as fichier:
                    while (mesDurees and mesFrequences):
                        fichier.write(str(mesFrequences[0])+";"+str(mesDurees[0])+"\n")
                        mesFrequences.pop(0)
                        mesDurees.pop(0)
                       
            except IOError:
                print("Error occurred! Make sure the file is properly closed.")
    
        if data and timestamp:
            timestamp = time.strftime('%H:%M:%S')
            print(f'{timestamp} > {data}')
        elif data:
            print(data)


if __name__ == '__main__':

    ser = ouvrirConnexion('COM6', 9600)
    transmettreDonnees(ser)
    readserial(ser, True)