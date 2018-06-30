/* SondePiscine
 *
 *	Simple sonde alimentée par des cellules photoélectriques 
 *	et qui revoie par MQTT la température de l'eau de la piscine.
 *
 *	provided by "Laurent Faillie"
 *	Licence : "Creative Commons Attribution-NonCommercial 3.0"
 *
 *	Dependances :
 *		- PubSubClient.h
 *			Il faut la version permettant un QoS > 0 (/ESP/TrucAstuces/MQTT_ESP8266/)
 *
 *
 *	22/05/2018 - La tension d'alimentation est calculée par plusieurs échantillonages
 *	21/06/2018 - Ajoute de l'OTA
 *	30/06/2018 - reconstruit pour externaliser la gestion du réseau
 */

	/*******
	* Paramétrage
	********/

#define DEV	// On est mode developpement

#ifdef DEV
#	define MQTT_CLIENT "SondePiscine-Dev"

	/* Message de Debug sur le tx.
	 * Pas de LED vu qu'il est sur le même PIN
	 */
#	define LED(x)	{ }
#	define SERIAL_ENABLED

#	define DEF_DUREE_SOMMEIL 30	// Sommeil entre 2 acquisitions
#else
#	define MQTT_CLIENT "SondePiscine"

	/* La LED est allumée pendant la recherche du réseau et l'établissement
	 * du MQTT
	 */
#	define LED(x)	{ digitalWrite(LED_BUILTIN, x); }
#endif
String MQTT_Topic = String(MQTT_CLIENT) + "/";	// Topic racine

	/* Paramètres par défaut */
	/* Durées (secondes) */
#ifndef DEF_DUREE_SOMMEIL
#	define DEF_DUREE_SOMMEIL 300	// Sommeil entre 2 acquisitions (5 minutes)
#endif
#define DEF_EVEILLE	60				// Durée où il faut rester éveillé après avoir recu une commande
#define DEF_DELAI_RECONNEXION	120		// delais pour re-essayer en cas de pb de connexion

	/******
	* Fin du paramétrage
	*******/

	/*******
	* Gestion des configurations
	********/

#include <KeepInRTC.h>
KeepInRTC kir;	// Gestionnaire de la RTC

#include <LFUtilities.h>
#include <LFUtilities/Duration.h>
#include <LFUtilities/TemporalConsign.h>

TemporalConsign Sommeil( kir );			// Sommeil entre 2 acquisitions
TemporalConsign EveilInteractif( kir );	// Temps pendant lequel il faut rester éveillé en attendant des ordres.

class Contexte : public KeepInRTC::KeepMe {
	struct {
		bool debug;				// Affiche des messages de debugage
		uint32_t reessaiWiFi;	// Délai avant de retester la connexion au WiFi
	} data;

public:
	Contexte() : KeepInRTC::KeepMe( kir, (uint32_t *)&this->data, sizeof(this->data) ) {}

	bool begin( void ){
		if( !kir.isValid() ){
			this->data.debug = false;
			this->data.reessaiWiFi = DEF_DELAI_RECONNEXION;
			this->save();
			return true;
		}
		return false;
	}

	void setDebug( bool adbg ){
		this->data.debug = adbg; 
		this->save();
	}

	bool getDebug( void ){
		return this->data.debug;
	}

	void setReEssaiWiFi( uint32_t val ){
		this->data.reessaiWiFi = val;
		this->save();
	}

	uint32_t getReEssaiWiFi( void ){
		return this->data.reessaiWiFi;
	}
} ctx;


	/*******
	* Reseau
	********/

#include <ESP8266WiFi.h>

#include <Maison.h>		// Paramètres de mon réseau
#include <LFUtilities/SafeMQTTClient.h>

WiFiClient clientWiFi;
SafeMQTTClient Reseau( clientWiFi, 
#ifdef DEV
		WIFI_SSID, WIFI_PASSWORD,	// Connexion à mon réseau domestique
#else
		DOMO_SSID, DOMO_PASSWORD,	// Connexion à mon réseau domotique
#endif
		BROKER_HOST, BROKER_PORT,
		MQTT_CLIENT, MQTT_Topic.c_str(), false
);


	/*******
	* OnTheAir
	********/

bool OTA;


	/*******
	* Gestion des commandes MQTT
	********/

bool func_status( const String & ){
	String msg = "Délai acquisition : ";
	msg += Sommeil.getConsign();
	msg += "\nEveil suite à commande : ";
	msg += EveilInteractif.getConsign();
	msg += "\nTemps maximum pour la connexion MQTT : ";
	msg += Reseau.getMQTTMaxTries();
	msg += "\nTemps maximum de reconnexion WiFi : ";
	msg += ctx.getReEssaiWiFi();
	msg += ctx.getDebug() ? "\nMessages de Debug" : "\nPas de message de Debug";
#ifdef DEV
	msg += "\nFlash : ";
	msg += ESP.getFlashChipSize();
	msg += " (real : ";
	msg += ESP.getFlashChipRealSize();
	msg += ")";
#endif
	msg += "\nSketch : ";
	msg += 	ESP.getSketchSize();
	msg += ", Libre : ";
	msg += ESP.getFreeSketchSpace();
	msg += "\nHeap :";
	msg += ESP.getFreeHeap();
	msg += "\nAdresse IP :";
	msg += WiFi.localIP().toString();

	msg += OTA ? "\nOTA en attente": "\nOTA désactivé";

	Reseau.logMsg( msg );
	return true;
}

const struct _command {
	const char *nom;
	const char *desc;
	bool (*func)( const String & );	// true : raz du timer d'éveil
} commands[] = {
	{ "status", "Configuration courante", func_status },
/*
	{ "delai", "Délai entre chaque échantillons (secondes)", func_delai },
	{ "attente", "Attend <n> secondes l'arrivée de nouvelles commandes", func_att },
	{ "maxMQTT", "Durée maximum d'attente pour se connecter au broker", func_maxMQTT },
	{ "reconnexion", "Attend <n> secondes avant de tenter de se reconnecter", func_reconnexion },
	{ "dodo", "Sort du mode interactif et place l'ESP en sommeil", func_dodo },
	{ "reste", "Reste encore <n> secondes en mode interactif", func_reste },
	{ "debug", "Active (1) ou non (0) les messages de debug", func_debug },
	{ "OTA", "Active l'OTA jusqu'au prochain reboot", func_OTA },
*/
	{ NULL, NULL, NULL }
};

void handleMQTT(char* topic, byte* payload, unsigned int length){
	String ordre;
	for(unsigned int i=0;i<length;i++)
		ordre += (char)payload[i];

#	ifdef SERIAL_ENABLED
	Serial.print( "Message [" );
	Serial.print( topic );
	Serial.print( "] : '" );
	Serial.print( ordre );
	Serial.println( "'" );
#	endif

		/* Extrait la commande et son argument */
	const int idx = ordre.indexOf(' ');
	String arg;
	if(idx != -1){
		arg = ordre.substring(idx + 1);
		ordre = ordre.substring(0, idx);
	}

	bool raz = true;	// Faut-il faire un raz du timer interactif
	if( ordre == "?" ){	// Liste des commandes connues
		String rep;
		if( arg.length() ) {
			rep = arg + " : ";

			for( const struct _command *cmd = commands; cmd->nom; cmd++ ){
				if( arg == cmd->nom && cmd->desc ){
					rep += cmd->desc;
					break;	// Pas besoin de continuer la commande a été trouvée
				}
			}
		} else {
			rep = "Liste des commandes reconnues :";

			for( const struct _command *cmd = commands; cmd->nom; cmd++ ){
				rep += ' ';
				rep += cmd->nom;
			}
		}

		Reseau.logMsg( rep );
	} else {
		for( const struct _command *cmd = commands; cmd->nom; cmd++ ){
			if( ordre == cmd->nom && cmd->func ){
				raz = cmd->func( arg );
				break;
			}
		}
	}

	if( raz )		// Raz du timer d'éveil
		EveilInteractif.setNext( millis() + EveilInteractif.getConsign() * 1e3 );
}


	/*******
	* Le code
	********/

void setup(){
#ifdef SERIAL_ENABLED
	Serial.begin(115200);
	delay(100);
	Serial.println("Réveil");
#else
	pinMode(LED_BUILTIN, OUTPUT);
#endif

	if( Sommeil.begin(DEF_DUREE_SOMMEIL) | EveilInteractif.begin(DEF_EVEILLE) | ctx.begin() ){	// ou logique sinon le begin() d'EveilInteractif ne sera jamais appelé
#ifdef SERIAL_ENABLED
		Serial.println("Valeur par défaut");
#endif
	}

	Reseau.getClient().setCallback( handleMQTT );
	LED(LOW);
	Duration dwifi;
	if(!Reseau.connectWiFi(dwifi)){	// Impossible de se connecter au WiFi
		LED(HIGH);
		ESP.deepSleep( ctx.getReEssaiWiFi() );	// On ressaiera plus tard
	}
	LED(HIGH);

}

void loop(){
	if(Reseau.connected())
		Reseau.getClient().loop();

	delay( 500 );
}

