import copy as cp
import numpy as np
import numpy.testing as npt
import pytest

import hoomd


def _evaluate_periodic(snapshot, params):
    """Evaluate force and energy in python for Periodic."""
    box = hoomd.Box(*snapshot.configuration.box)
    positions = snapshot.particles.position
    A = params['A']
    i = params['i']
    w = params['w']
    p = params['p']
    a1, a2, a3 = box.lattice_vectors
    V = np.dot(a1, np.cross(a2, a3))
    b1 = 2 * np.pi / V * np.cross(a2, a3)
    b2 = 2 * np.pi / V * np.cross(a3, a1)
    b3 = 2 * np.pi / V * np.cross(a1, a2)
    b = {0: b1, 1: b2, 2: b3}.get(i)
    energies = A * np.tanh(
        1 / (2 * np.pi * p * w) * np.cos(p * np.dot(positions, b)))
    forces = A / (2 * np.pi * w) * np.sin(p * np.dot(positions, b))
    forces *= 1 - (np.tanh(
        np.cos(p * np.dot(positions, b)) / (2 * np.pi * p * w)))**2
    forces = np.outer(forces, b)
    return forces, energies


def _evaluate_electric(snapshot, params):
    """Evaluate force and energy in python for ElectricField."""
    positions = snapshot.particles.position
    charges = snapshot.particles.charge
    E_field = params
    energies = -charges * np.dot(positions, E_field)
    forces = np.outer(charges, E_field)
    return forces, energies

def _evaluate_gravitational(snapshot, params):
    """Evaluate force and energy in python for GravitationalField."""
    positions = snapshot.particles.position
    masses = snapshot.particles.mass
    g_field = params
    energies = -masses * np.dot(positions, g_field)
    forces = np.outer(masses, g_field)
    return forces, energies


def _external_params():
    """Each is tuple (cls_obj, param attr, lis(param values), eval func)."""
    list_ext_params = []
    list_ext_params.append(
        (hoomd.md.external.field.Periodic, "params",
         list([dict(A=1.5, i=1, w=3.5, p=5),
               dict(A=10, i=0, w=3.4, p=2)]), _evaluate_periodic))
    list_ext_params.append(
        (hoomd.md.external.field.Electric, "E", list([
            (1, 0, 0),
            (0, 2, 0),
        ]), _evaluate_electric))
    return list_ext_params


@pytest.fixture(scope="function",
                params=_external_params(),
                ids=(lambda x: x[0].__name__))
def external_params(request):
    return cp.deepcopy(request.param)


def _assert_correct_params(external_obj, param_attr, params):
    """Assert the params of the external object match whats in the dict."""
    if type(params) == dict:
        for param in params.keys():
            npt.assert_allclose(
                getattr(external_obj, param_attr)['A'][param], params[param])
    if type(params) == tuple:
        npt.assert_allclose(getattr(external_obj, param_attr)['A'], params)


def test_get_set(simulation_factory, two_particle_snapshot_factory,
                 external_params):
    """Test we can get/set parameter while attached and while not attached."""
    # unpack parameters
    cls_obj, param_attr, list_params, evaluator = external_params

    # create class instance, get/set params when not attached
    obj_instance = cls_obj()
    getattr(obj_instance, param_attr)['A'] = list_params[0]
    _assert_correct_params(obj_instance, param_attr, list_params[0])
    getattr(obj_instance, param_attr)['A'] = list_params[1]
    _assert_correct_params(obj_instance, param_attr, list_params[1])

    # set up simulation
    snap = two_particle_snapshot_factory(d=3.7)
    sim = simulation_factory(snap)
    sim.operations.integrator = hoomd.md.Integrator(dt=0.001)
    sim.operations.integrator.forces.append(obj_instance)
    sim.run(0)

    # get/set params while attached
    getattr(obj_instance, param_attr)['A'] = list_params[0]
    _assert_correct_params(obj_instance, param_attr, list_params[0])
    getattr(obj_instance, param_attr)['A'] = list_params[1]
    _assert_correct_params(obj_instance, param_attr, list_params[1])


def test_forces_and_energies(simulation_factory, lattice_snapshot_factory,
                             external_params):
    """Run a small simulation and make sure forces/energies are correct."""
    # unpack parameters
    cls_obj, param_attr, list_params, evaluator = external_params

    for param in list_params:
        # create class instance
        obj_instance = cls_obj()
        getattr(obj_instance, param_attr)['A'] = param

        # set up simulation and run a bit
        snap = lattice_snapshot_factory(n=2)
        if snap.communicator.rank == 0:
            snap.particles.charge[:] = np.random.random(
                snap.particles.N) * 2 - 1
        sim = simulation_factory(snap)
        sim.operations.integrator = hoomd.md.Integrator(dt=0.001)
        sim.operations.integrator.forces.append(obj_instance)
        sim.run(10)

        # test energies
        new_snap = sim.state.snapshot
        forces = sim.operations.integrator.forces[0].forces
        energies = sim.operations.integrator.forces[0].energies
        if new_snap.communicator.rank == 0:
            expected_forces, expected_energies = evaluator(new_snap, param)
            np.testing.assert_allclose(expected_forces, forces)
            np.testing.assert_allclose(expected_energies, energies)
