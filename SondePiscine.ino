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
SafeMQTTClient reseau( clientWiFi, 
#ifdef DEV
		WIFI_SSID, WIFI_PASSWORD,	// Connexion à mon réseau domestique
#else
		DOMO_SSID, DOMO_PASSWORD,	// Connexion à mon réseau domotique
#endif
		BROKER_HOST, BROKER_PORT,
		MQTT_CLIENT, MQTT_Topic.c_str(), false
);


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

	LED(LOW);
	Duration dwifi;
	if(!reseau.connectWiFi(dwifi)){	// Impossible de se connecter au WiFi
		LED(HIGH);
		ESP.deepSleep( ctx.getReEssaiWiFi() );	// On ressaiera plus tard
	}
	LED(HIGH);

}

void loop(){
	if(reseau.connected())
		reseau.getClient().loop();

	delay( 500 );
}

