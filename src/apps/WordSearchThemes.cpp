#include "GameLogic.h"

// Word banks for Word Search. Kept const so they stay in flash.
// Upper case A-Z only, 3 to 10 letters.

namespace Games {
namespace {

const char* const kSea[] = {
    "ANCHOR", "BOWSPRIT", "CAPSTAN", "CLIPPER", "COMPASS", "GALLEY", "HARBOUR", "HULL", "KEEL",
    "MAST", "RIGGING", "RUDDER", "SAIL", "SCHOONER", "STERN", "TIDE", "VOYAGE", "WAKE",
    "BOSUN", "CABIN", "DECK", "FATHOM", "HELM", "ISLAND", "CARGO", "HAMMOCK", "SEXTANT", "BALLAST",
    "TILLER", "PORTHOLE", "ROPE", "BUOY",
};

const char* const kGarden[] = {
    "ROSE", "TULIP", "DAISY", "HEDGE", "SPADE", "TROWEL", "COMPOST", "MARIGOLD", "LAVENDER",
    "POPPY", "IVY", "FERN", "ORCHARD", "BLOSSOM", "SEEDLING", "PRIMROSE", "FOXGLOVE", "BRAMBLE",
    "MEADOW", "PEONY", "THISTLE", "GREENHOUSE", "BLUEBELL", "SNOWDROP", "LUPIN", "RAKE", "WILLOW",
    "LAWN", "PANSY", "BULB",
};

const char* const kKitchen[] = {
    "FLOUR", "BUTTER", "SCONE", "KETTLE", "OVEN", "WHISK", "LADLE", "PANTRY", "CRUMPET",
    "MUFFIN", "PASTRY", "SUGAR", "YEAST", "DOUGH", "TOAST", "JAM", "RECIPE", "APRON",
    "SAUCEPAN", "CUSTARD", "TREACLE", "BISCUIT", "MARMALADE", "SPONGE", "GRIDDLE", "GRAVY",
    "PUDDING", "COLANDER", "SIEVE", "FRITTER",
};

const char* const kWeather[] = {
    "RAIN", "SNOW", "SLEET", "HAIL", "FOG", "MIST", "THUNDER", "LIGHTNING", "BREEZE", "GALE",
    "STORM", "CLOUD", "RAINBOW", "DRIZZLE", "FROST", "SUNSHINE", "MONSOON", "BAROMETER",
    "HURRICANE", "TORNADO", "DEW", "HUMID", "BLIZZARD", "SQUALL", "CYCLONE", "THAW", "ICICLE",
    "FORECAST", "DROUGHT", "TYPHOON",
};

const char* const kBirds[] = {
    "ROBIN", "WREN", "SPARROW", "OWL", "HERON", "KESTREL", "SWALLOW", "PUFFIN", "GANNET",
    "MAGPIE", "CUCKOO", "FINCH", "STARLING", "BLACKBIRD", "THRUSH", "EAGLE", "FALCON", "OSPREY",
    "CURLEW", "PLOVER", "LAPWING", "PELICAN", "GULL", "RAVEN", "JAY", "SKYLARK", "KINGFISHER",
    "NUTHATCH", "BITTERN", "DIPPER",
};

const char* const kBooks[] = {
    "NOVEL", "CHAPTER", "PAGE", "SPINE", "INDEX", "POETRY", "AUTHOR", "LIBRARY", "BOOKMARK",
    "PREFACE", "PROLOGUE", "EPILOGUE", "VERSE", "STORY", "FABLE", "ATLAS", "ESSAY", "MARGIN",
    "CHARACTER", "PLOT", "SONNET", "READER", "PAPER", "INK", "QUILL", "GLOSSARY", "FOOTNOTE",
    "HEROINE", "MYSTERY", "LEGEND",
};

const char* const kMusic[] = {
    "PIANO", "VIOLIN", "CELLO", "FLUTE", "OBOE", "HARP", "TRUMPET", "TROMBONE", "CLARINET",
    "BASSOON", "DRUM", "CHORD", "MELODY", "RHYTHM", "TEMPO", "SONATA", "CHOIR", "ORCHESTRA",
    "OPERA", "SCALE", "TUBA", "LUTE", "BALLAD", "OCTAVE", "HARMONY", "SYMPHONY", "CONCERTO",
    "CYMBAL", "BUGLE", "FIDDLE",
};

const char* const kSky[] = {
    "STAR", "MOON", "COMET", "PLANET", "ORBIT", "GALAXY", "NEBULA", "METEOR", "ECLIPSE",
    "TELESCOPE", "ORION", "SATURN", "JUPITER", "VENUS", "MARS", "MERCURY", "NEPTUNE", "URANUS",
    "AURORA", "CRATER", "ZENITH", "PULSAR", "QUASAR", "COSMOS", "ASTEROID", "SOLSTICE",
    "EQUINOX", "TWILIGHT", "SUPERNOVA", "CRESCENT",
};

const char* const kTea[] = {
    "ASSAM", "DARJEELING", "OOLONG", "CEYLON", "JASMINE", "PEKOE", "CHAI", "TEAPOT", "SAUCER",
    "INFUSION", "CLOVE", "NUTMEG", "PEPPER", "SAFFRON", "CARDAMOM", "CINNAMON", "GINGER",
    "VANILLA", "MACE", "ANISE", "CUMIN", "TURMERIC", "CADDY", "STRAINER", "BERGAMOT", "SENCHA",
    "MATCHA", "KETTLE", "TEACUP", "CHAMOMILE",
};

const char* const kCoast[] = {
    "BEACON", "CLIFF", "SHINGLE", "SURF", "LANTERN", "KEEPER", "REEF", "ROCK", "FOGHORN",
    "HEADLAND", "COVE", "PIER", "JETTY", "DUNE", "BREAKWATER", "LIGHTHOUSE", "SEAWEED", "LIMPET",
    "CRAB", "SPRAY", "SWELL", "ESTUARY", "LAGOON", "PEBBLE", "ROCKPOOL", "HARBOUR", "SANDBANK",
    "DRIFTWOOD", "SEAGULL", "BAY",
};

const char* const kCities[] = {
    "PARIS", "ROME", "LONDON", "CAIRO", "LIMA", "OSLO", "DELHI", "TOKYO", "BERLIN", "MADRID",
    "VIENNA", "ATHENS", "DUBLIN", "LISBON", "PRAGUE", "NAIROBI", "OTTAWA", "HAVANA", "SYDNEY",
    "MOSCOW", "BEIJING", "SEOUL", "QUITO", "WARSAW", "BUDAPEST", "STOCKHOLM", "HELSINKI",
    "BANGKOK", "SANTIAGO", "EDINBURGH",
};

const char* const kWoodland[] = {
    "BADGER", "FOX", "HEDGEHOG", "SQUIRREL", "OTTER", "STOAT", "WEASEL", "RABBIT", "HARE",
    "DEER", "MOLE", "VOLE", "SHREW", "DORMOUSE", "TOAD", "NEWT", "OAK", "BEECH", "ACORN",
    "BRACKEN", "MOSS", "TOADSTOOL", "WOODPECKER", "BLUEBELL", "CHESTNUT", "BIRCH", "HAZEL",
    "FUNGUS", "HOLLY", "BURROW",
};

#define THEME(name, list) {name, list, (int)(sizeof(list) / sizeof(list[0]))}
const WordTheme kThemes[] = {
    THEME("Sea & Ships", kSea),
    THEME("The Garden", kGarden),
    THEME("Kitchen", kKitchen),
    THEME("Weather", kWeather),
    THEME("Birds", kBirds),
    THEME("Books", kBooks),
    THEME("Music", kMusic),
    THEME("Night Sky", kSky),
    THEME("Tea & Spice", kTea),
    THEME("The Coast", kCoast),
    THEME("Capital Cities", kCities),
    THEME("The Woods", kWoodland),
};
#undef THEME

} // namespace

int wordThemeCount() { return (int)(sizeof(kThemes) / sizeof(kThemes[0])); }

const WordTheme& wordTheme(int index) {
    if (index < 0 || index >= wordThemeCount()) index = 0;
    return kThemes[index];
}

} // namespace Games
