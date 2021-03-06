class HandlerError(Exception):
  def __init__(self, value):
    self.__value = value

  def __str__(self):
    return repr(self.__value)


import inspect
import logging
import sys

from organism import OrganismError
from swig_modules.automata import AnimalMetabolism, PlantMetabolism
import user_handlers


logger = logging.getLogger(__name__)


""" Defines a common superclass for all update handlers. An update handler is
something that gets run every iteration of the simulation on a filtered subset
of the organisms being updated. This framework is designed so that users
can easily implement custom handlers. """
class UpdateHandler:
  """ A list of all the handlers currently known to this simulation. """
  handlers = []

  """ Checks if an organism matches the static filtering criteria for all the
  registered handlers, and add the handler to the organism's list of handlers.
  """
  @classmethod
  def set_handlers_static_filtering(cls, organism):
    for handler in cls.handlers:
      if handler.check_static_filters(organism):
        # It meets the criteria.
        organism.add_handler(handler)

        # Run the handler setup function on the organism.
        handler.setup(organism)

  """ All subclasses should call this constructor. """
  def __init__(self):
    # A dictionary storing what attribute values we are filtering for.
    self.__static_filters = {}

    # Register handler.
    logger.info("Registering handler '%s'." % (self.__class__.__name__))
    UpdateHandler.handlers.append(self)

  """ Specifies that only organisms that have a particular attribute set in a
  particular way will be handled by this handler. These handlers will be run
  once upon creation of every new organism.
  attribute: Specified attribute.
  values: What that attribute needs to equal. Can be a list if there are
  multiple things. """
  def filter_attribute(self, attribute, value):
    collection = (type(value) == list or type(value) == tuple)
    if not collection:
      if attribute in self.__static_filters.keys():
        # A single value for an attribute we have added.
        self.__static_filters[attribute].append(value)
      else:
        # A single value for an attribute we haven't yet added.
        self.__static_filters[attribute] = [value]
    else:
      if attribute in self.__static_filters.keys():
        # A collection for an attribute we have added.
        self.__static_filters[attribute].extend(list(value))
      else:
        # A collection for an attribute we haven't yet added.
        self.__static_filters[attribute] = list(value)

  """ Specifies a custom dynamic filter that every organism that meets the
  criteria of the static filters gets put through every time it is being
  updated.
  organism: The organism to check if we should handle.
  Returns: True if we should handle this organism, false if we shouldn't. """
  def dynamic_filter(self, organism):
    # By default, do nothing.
    return True

  """ A particular set of actions that must be taken when this handler is first
  added to an organism. """
  def setup(self, organism):
    pass

  """ Runs the actual body of the handler. This is designed to be implemented by
  the user in superclasses.
  organism: The organism to run the handler on.
  iteration_time: How much simulation time passed since the last time we ran the
                  handler. """
  def run(self, organism, iteration_time):
    raise NotImplementedError("'run' must be implemented by subclass.")

  """ Determines whether a paticular organism meets the static filtering
  criteria for this handler.
  organism: The organism to check.
  Returns: True if it does, false if it doesn't."""
  def check_static_filters(self, organism):
    for attribute in self.__static_filters.keys():
      # Handle nested attributes intelligently.
      lowest_attribute = organism
      for subcategory in attribute.split("."):
        try:
          lowest_attribute = getattr(lowest_attribute, subcategory)
        except AttributeError:
          # This attribute doesn't exist.
          return False

      if lowest_attribute not in self.__static_filters[attribute]:
        # It doesn't meet the criteria.
        return False

    # Only if we get through everything does it meet the criteria.
    return True

  """ Checks the dynamic filters, and if the organism passes, it calls run.
  organism: The organism to run the handler on.
  iteration_time: Simulation time since when we last ran this. """
  def handle_organism(self, organism, iteration_time):
    if self.dynamic_filter(organism):
      self.run(organism, iteration_time)


""" Handler for animals. """
class AnimalHandler(UpdateHandler):
  def __init__(self):
    super().__init__()

    self.filter_attribute("Taxonomy.Kingdom", ["Opisthokonta", "Animalia"])

  def setup(self, organism):
    # Setup the metabolism simulator.
    logger.debug("Initializing metabolism simulation for organism %d." % \
                  (organism.get_index()))

    mass = organism.Metabolism.Animal.InitialMass
    fat_mass = organism.Metabolism.Animal.InitialFatMass
    body_temp = organism.Metabolism.Animal.BodyTemperature
    scale = organism.Scale
    drag_coefficient = organism.Metabolism.Animal.DragCoefficient

    args = [mass, fat_mass, body_temp, scale, drag_coefficient]
    logger.debug("Constructing AnimalMetabolism with args: %s" % (args))
    organism.metabolism = AnimalMetabolism(*args)

    # Set up the organism's vision.
    logger.debug("Initializing organism vision as %d." % \
                 (organism.Vision))
    organism.set_vision(organism.Vision)

  def run(self, organism, iteration_time):
    old_position = organism.get_position()
    logger.debug("Old position of %d: %s" % \
        (organism.get_index(), old_position))

    # Update animal position.
    try:
      print("Updating position.")
      organism.update_position()
      print("Updated position.")
    except OrganismError:
      # Check to see if we have a conflict we can resolve.
      organism.handle_conflict()

    new_position = organism.get_position()
    logger.debug("New position of %d: %s" % \
        (organism.get_index(), new_position))

    # Update the metabolism simulator for this time step.
    organism.metabolism.Update(iteration_time)
    logger.debug("Animal mass: %f, Animal energy: %f" % \
                (organism.metabolism.mass(), organism.metabolism.energy()))

    # Figure out energy specifically expended for movement.
    move_distance = ((new_position[0] - old_position[0]) ** 2 + \
                     (new_position[1] - old_position[1]) ** 2) ** (0.5)
    organism.metabolism.Move(move_distance, iteration_time)

    # Organism should die if it runs out of energy.
    if organism.metabolism.energy() <= 0:
      logger.info("Killing organism due to lack of energy.")
      organism.die()


""" Handler for plants. """
class PlantHandler(UpdateHandler):
  def __init__(self):
    super().__init__()

    self.filter_attribute("Taxonomy.Kingdom", "Plantae")

  def setup(self, organism):
    # Setup the metabolism simulation.
    logger.debug("Initializing metabolism simulation for organism %d." %
                  (organism.get_index()))

    # Figure out efficiency.
    if organism.Metabolism.Photosynthesis.Pathway == "C3":
      efficiency = organism.Metabolism.Photosynthesis.C3Efficiency
    elif organism.Metabolism.Photosynthesis.Pathway == "C4":
      efficiency = organism.Metabolism.Photosynthesis.C4Efficiency
    else:
      raise ValueError("Invalid photosynthesis pathway: '%s'" % \
                        (organism.Metabolism.Photosynthesis.Pathway))

    mass = organism.Metabolism.Plant.SeedlingMass

    # Figure out the amount of leaf area.
    try:
      area_mean = organism.Metabolism.Plant.MeanLeafArea
    except AttributeError:
      logger.warning("Using default leaf area mean for plant '%d'." % \
                      (organism.get_index()))
      # Calculate a plausible leaf area based on the scale.
      area_mean = 0.5 * (organism.Scale ** 2)
    try:
      area_stddev = organism.Metabolism.Plant.LeafAreaStddev
    except AttributeError:
      logger.warning("Using default leaf area stddev for plant '%d'." % \
                      (organism.get_index()))
      # Calculate a plausible leaf standard deviation based on the area.
      area_stddev = area_mean * 0.3

    cellulose = organism.Metabolism.Plant.Cellulose
    hemicellulose = organism.Metabolism.Plant.Hemicellulose
    lignin = organism.Metabolism.Plant.Lignin

    args = [mass, efficiency, area_mean, area_stddev, cellulose,
            hemicellulose, lignin]
    logger.debug("Constructing PlantMetabolism with args: %s" % (args))
    organism.metabolism = PlantMetabolism(*args)

  def run(self, organism, iteration_time):
    # Request that the plant stays in the same place. (If we don't do this, it
    # won't generate a conflict if something else tries to move here.)
    logger.debug("Plant position: %s" % (str(organism.get_position())))
    organism.set_position(organism.get_position())

    # Update the metabolism simulator for this time step.
    organism.metabolism.Update(iteration_time)
    logger.debug("Plant mass: %f, energy: %f" % \
        (organism.metabolism.mass(), organism.metabolism.energy()))

    # Organism should die if it runs out of energy.
    if organism.metabolism.energy() <= 0:
      logger.info("Killing organism due to lack of energy.")
      organism.die()


# Go and register all the update handlers.
handlers = inspect.getmembers(sys.modules["user_handlers"],
    inspect.isclass)
handlers.extend(inspect.getmembers(sys.modules[__name__], inspect.isclass))
for handler in handlers:
  if (issubclass(handler[1], UpdateHandler) and handler[0] != "UpdateHandler"):
    # We want to filter to only handlers.
    handler[1]()
